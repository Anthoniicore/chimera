// SPDX-License-Identifier: GPL-3.0-only

#include "../../../command/command.hpp"
#include "../../../event/connect.hpp"
#include "../../../event/frame.hpp"
#include "../../../event/tick.hpp"
#include "../../../halo_data/multiplayer.hpp"
#include "../../../halo_data/player.hpp"
#include "../../../output/draw_text.hpp"
#include "../../../output/output.hpp"
#include "../../../voice_chat/audio_capture.hpp"
#include "../../../voice_chat/voice_chat.hpp"
#include "../../../voice_chat/voice_transport.hpp"
#include "voice.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace Chimera {
    namespace {
        bool g_voice_frame_registered = false;
        unsigned int g_push_to_talk_key = 'V';
        std::uint32_t g_sequence = 0;
        bool g_manual_transport_override = false;
        bool g_manual_voice_channel_override = false;

        constexpr std::uint32_t VOICE_NO_PLAYER_SENDER_ID = 0xFFFFFFFFu;

        constexpr std::uint16_t DEFAULT_VOICE_PORT = 30777;
        constexpr std::uint16_t DEFAULT_HALO_PORT = 2302;

        // How often to send a keepalive while connected but not talking, to
        // stop this client's own NAT mapping (and the relay's per-room
        // timeout) from expiring during conversational silence.
        constexpr DWORD KEEPALIVE_INTERVAL_MS = 20000;
        DWORD g_last_voice_send_tick = 0;

        std::string player_name_to_narrow(const wchar_t *name) {
            char buffer[64] = {};
            if(WideCharToMultiByte(CP_UTF8, 0, name, -1, buffer, sizeof(buffer) - 1, nullptr, nullptr) == 0) {
                return std::string();
            }
            return std::string(buffer);
        }

        Player *voice_player_by_machine_index(std::uint32_t machine_index) noexcept {
            auto &table = PlayerTable::get_player_table();
            const auto count = std::min<std::size_t>(table.current_size, 16);
            for(std::size_t i = 0; i < count; ++i) {
                auto &player = table.first_element[i];
                if(player.player_id != 0xFFFF && player.machine_index == machine_index) {
                    return &player;
                }
            }
            return nullptr;
        }

        std::string voice_sender_display_name(std::uint32_t sender_id) {
            if(sender_id == VOICE_NO_PLAYER_SENDER_ID) return "Unknown";
            auto *player = voice_player_by_machine_index(sender_id);
            if(!player) return "Unknown";
            auto name = player_name_to_narrow(player->name);
            return name.empty() ? "Unknown" : name;
        }

        std::uint32_t voice_sender_id() noexcept {
            auto *player = PlayerTable::get_player_table().get_client_player();
            if(player) return static_cast<std::uint32_t>(player->machine_index);
            return VOICE_NO_PLAYER_SENDER_ID;
        }

        std::uint32_t voice_timestamp() noexcept {
            return static_cast<std::uint32_t>(GetTickCount());
        }

        void update_voice_channel_from_game() noexcept {
            if(g_manual_voice_channel_override) return;

            // Only query the current gametype after Halo reports that we are
            // actually connected. This avoids reading current_gametype_sig
            // during the pre-connect phase.
            if(server_type() == SERVER_NONE) return;

            set_voice_chat_channel(
                is_team() ? VoiceChatChannel::TEAM : VoiceChatChannel::ALL
            );
        }

        void draw_voice_speaker_overlay() noexcept {
            auto speakers = get_active_voice_speakers();
            if(speakers.empty()) return;

            std::sort(speakers.begin(), speakers.end());

            static const ColorARGB speaking_color{1.0, 0.35, 1.0, 0.4};
            auto font = GenericFont::FONT_SMALL;
            const std::int16_t increment = font_pixel_height(font);
            std::int16_t y = 8;
            constexpr std::int16_t x = 8;
            constexpr std::int16_t width = 200;

            for(auto id : speakers) {
                std::string line = ">> " + voice_sender_display_name(id);
                apply_text(line, x, y, width, increment, speaking_color, font, FontAlignment::ALIGN_LEFT, TextAnchor::ANCHOR_TOP_LEFT);
                y += increment;
            }
        }

        void voice_frame_update() noexcept {
            if(!voice_chat_enabled()) return;

            // Automatically select TEAM for team-based variants and ALL for
            // FFA variants. Manual voice_all/team commands can override this.
            update_voice_channel_from_game();

            process_received_voice_packets();
            draw_voice_speaker_overlay();

            const bool talking = (GetAsyncKeyState(static_cast<int>(g_push_to_talk_key)) & 0x8000) != 0;
            if(talking) {
                bool sent_any = false;
                while(send_pending_voice_packet(voice_sender_id(), g_sequence, voice_timestamp())) { sent_any = true; }
                if(sent_any) g_last_voice_send_tick = GetTickCount();
            }
            else {
                std::vector<std::int16_t> discarded;
                while(consume_voice_audio_packet(discarded)) {}

                const auto now = GetTickCount();
                if(now - g_last_voice_send_tick >= KEEPALIVE_INTERVAL_MS) {
                    if(send_voice_keepalive_packet(voice_sender_id())) g_last_voice_send_tick = now;
                }
            }
        }

        void update_voice_frame_registration() noexcept {
            if(voice_chat_enabled() && !g_voice_frame_registered) {
                add_preframe_event(voice_frame_update, EventPriority::EVENT_PRIORITY_FINAL);
                g_voice_frame_registered = true;
            }
            else if(!voice_chat_enabled() && g_voice_frame_registered) {
                remove_preframe_event(voice_frame_update);
                g_voice_frame_registered = false;
            }
        }

        bool parse_port(const char *text, std::uint16_t &port) noexcept {
            if(!text || !*text) return false;
            char *end = nullptr;
            const auto value = std::strtoul(text, &end, 10);
            if(end == text || *end != '\0' || value == 0 || value > std::numeric_limits<std::uint16_t>::max()) return false;
            port = static_cast<std::uint16_t>(value);
            return true;
        }

        bool parse_virtual_key(const char *text, unsigned int &key) noexcept {
            if(!text || !*text) return false;
            if(std::strlen(text) == 1) {
                key = static_cast<unsigned char>(text[0]);
                return true;
            }
            char *end = nullptr;
            const auto value = std::strtoul(text, &end, 0);
            if(end == text || *end != '\0' || value > 255) return false;
            key = static_cast<unsigned int>(value);
            return true;
        }

        std::uint32_t fnv1a(const std::string &text) noexcept {
            std::uint32_t hash = 0x811C9DC5u;
            for(unsigned char c : text) {
                hash ^= c;
                hash *= 0x01000193u;
            }
            return hash;
        }

        std::string to_lower(std::string text) noexcept {
            for(auto &c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        std::uint32_t voice_room_id_for_server(const std::string &host, std::uint16_t port) noexcept {
            auto room_id = fnv1a(to_lower(host) + ":" + std::to_string(port));
            return room_id == 0 ? 1 : room_id;
        }

        bool voice_on_preconnect(std::uint32_t &ip, std::uint16_t &port, const char *password) noexcept {
            (void)password;

            std::uint8_t *ip_chars = reinterpret_cast<std::uint8_t *>(&ip);
            char host[16] = {};
            std::snprintf(host, sizeof(host), "%u.%u.%u.%u", ip_chars[3], ip_chars[2], ip_chars[1], ip_chars[0]);

            set_voice_chat_room(voice_room_id_for_server(host, port));

            if(!g_manual_transport_override) {
                set_voice_chat_transport(host, DEFAULT_VOICE_PORT);
            }

            // Every new server connection starts in automatic ALL/TEAM mode.
            // The actual game type is read once the game is active.
            g_manual_voice_channel_override = false;

            if(!voice_chat_enabled()) {
                set_voice_chat_enabled(true);
                update_voice_frame_registration();
            }

            return true;
        }

        void voice_watch_disconnect() noexcept {
            static ServerType last_server_type = SERVER_NONE;
            auto current = server_type();
            if(last_server_type != SERVER_NONE && current == SERVER_NONE) {
                set_voice_chat_room(0);
                g_manual_voice_channel_override = false;
                set_voice_chat_channel(VoiceChatChannel::ALL);
                if(voice_chat_enabled()) {
                    set_voice_chat_enabled(false);
                    update_voice_frame_registration();
                }
            }
            last_server_type = current;
        }
    }

    void set_up_voice_connection_watcher() noexcept {
        add_preconnect_event(voice_on_preconnect);
        add_tick_event(voice_watch_disconnect);
    }

    bool voice_command(int argc, const char **argv) {
        if(argc == 1) {
            if(std::strcmp(argv[0], "1") == 0 || std::strcmp(argv[0], "true") == 0 || std::strcmp(argv[0], "on") == 0) {
                set_voice_chat_enabled(true);
            }
            else if(std::strcmp(argv[0], "0") == 0 || std::strcmp(argv[0], "false") == 0 || std::strcmp(argv[0], "off") == 0) {
                set_voice_chat_enabled(false);
            }
            else {
                console_output("Expected 0/1, false/true, or off/on.");
                return false;
            }
        }
        else if(argc != 0) {
            console_output("Usage: chimera_voice [on|off]");
            return false;
        }

        update_voice_frame_registration();
        console_output(voice_chat_enabled() ? "true" : "false");
        return true;
    }

    bool voice_all_command(int argc, const char **argv) {
        (void)argv;
        if(argc != 0) {
            console_output("Usage: chimera_voice_all");
            return false;
        }

        g_manual_voice_channel_override = true;
        set_voice_chat_channel(VoiceChatChannel::ALL);
        console_output("Voice channel: ALL (manual override)");
        return true;
    }

    bool voice_team_command(int argc, const char **argv) {
        (void)argv;
        if(argc != 0) {
            console_output("Usage: chimera_voice_team");
            return false;
        }

        g_manual_voice_channel_override = true;
        set_voice_chat_channel(VoiceChatChannel::TEAM);
        console_output("Voice channel: TEAM (manual override)");
        return true;
    }

    bool voice_host_command(int argc, const char **argv) {
        if(argc == 2) {
            std::uint16_t port = 0;
            if(!parse_port(argv[1], port) || !set_voice_chat_transport(argv[0], port)) {
                console_output("Unable to configure voice relay.");
                return false;
            }
            g_manual_transport_override = true;
            console_output("Voice relay configured: %s:%u (manual override - auto-connect won't change this)", argv[0], static_cast<unsigned int>(port));
            return true;
        }
        console_output("Usage: chimera_voice_host <host> <port>");
        return false;
    }

    bool voice_ptt_command(int argc, const char **argv) {
        if(argc == 1) {
            unsigned int key = 0;
            if(!parse_virtual_key(argv[0], key)) {
                console_output("Invalid virtual key. Use one character or a numeric VK code.");
                return false;
            }
            g_push_to_talk_key = key;
        }
        else if(argc != 0) {
            console_output("Usage: chimera_voice_ptt [key|vk_code]");
            return false;
        }

        if(g_push_to_talk_key >= 32 && g_push_to_talk_key <= 126) {
            console_output("PTT key: %c (%u)", static_cast<char>(g_push_to_talk_key), g_push_to_talk_key);
        }
        else {
            console_output("PTT VK code: %u", g_push_to_talk_key);
        }
        return true;
    }

    bool voice_speakers_command(int argc, const char **argv) {
        (void)argv;
        if(argc != 0) {
            console_output("Usage: chimera_voice_speakers");
            return false;
        }

        auto speakers = get_active_voice_speakers();
        if(speakers.empty()) {
            console_output("No one is talking right now.");
            return true;
        }

        for(auto id : speakers) {
            console_output(ConsoleColor{1.0, 0.25, 1.0, 0.25}, "%s is talking.", voice_sender_display_name(id).c_str());
        }
        return true;
    }

    bool voice_status_command(int argc, const char **argv) {
        (void)argv;
        if(argc != 0) {
            console_output("Usage: chimera_voice_status");
            return false;
        }

        console_output("Voice enabled: %s", voice_chat_enabled() ? "yes" : "no");
        console_output("Voice channel: %s%s",
            voice_chat_channel() == VoiceChatChannel::TEAM ? "TEAM" : "ALL",
            g_manual_voice_channel_override ? " (manual)" : " (automatic)");
        console_output("Local socket ready: %s", voice_transport_initialized() ? "yes" : "no");
        console_output("Relay destination set: %s", voice_transport_has_destination() ? "yes" : "no");
        console_output("Current room ID: 0x%08X%s", static_cast<unsigned int>(voice_chat_room()), voice_chat_room() == 0 ? " (not connected to a server)" : "");
        console_output("Packets sent: %u", static_cast<unsigned int>(voice_packets_sent_count()));
        console_output("Packets received: %u", static_cast<unsigned int>(voice_packets_received_count()));
        console_output("Packets from a different room (ignored): %u", static_cast<unsigned int>(voice_packets_wrong_room_count()));

        auto *player = PlayerTable::get_player_table().get_client_player();
        if(player) {
            console_output("Your machine_index (sender ID): %u", static_cast<unsigned int>(player->machine_index));
        }
        else {
            console_output("Your machine_index (sender ID): unavailable (not in a game)");
        }
        return true;
    }
}
