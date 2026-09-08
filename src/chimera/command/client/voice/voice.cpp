// SPDX-License-Identifier: GPL-3.0-only

#include "../../../command/command.hpp"
#include "../../../event/command.hpp"
#include "../../../event/frame.hpp"
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

        constexpr std::uint32_t VOICE_NO_PLAYER_SENDER_ID = 0xFFFFFFFFu;

        // The port every server admin's voice companion (tools/voice_relay.py)
        // should listen on. When connecting to a server, this client defaults
        // to that server's own IP on this port -- so voice chat "just works"
        // for any server whose admin also runs the relay on their own
        // machine (the same box already hosting their dedicated server),
        // with no dependency on any one central machine. Overridable per
        // session with chimera_voice_host, e.g. if an admin prefers to run
        // the relay on a separate box from the game server.
        constexpr std::uint16_t DEFAULT_VOICE_PORT = 30777;
        constexpr std::uint16_t DEFAULT_HALO_PORT = 2302;

        // Convert a Halo (UTF-16) player name to a narrow string for display.
        std::string player_name_to_narrow(const wchar_t *name) {
            char buffer[64] = {};
            if(WideCharToMultiByte(CP_UTF8, 0, name, -1, buffer, sizeof(buffer) - 1, nullptr, nullptr) == 0) {
                return std::string();
            }
            return std::string(buffer);
        }

        // Look up a display name for a voice sender ID. The sender ID is the
        // speaker's machine_index at the time they transmitted, which
        // PlayerTable::get_player_by_rcon_id() maps back to a live Player.
        std::string voice_sender_display_name(std::uint32_t sender_id) {
            if(sender_id == VOICE_NO_PLAYER_SENDER_ID) return "Unknown";
            auto *player = PlayerTable::get_player_table().get_player_by_rcon_id(sender_id);
            if(!player) return "Unknown";
            auto name = player_name_to_narrow(player->name);
            return name.empty() ? "Unknown" : name;
        }

        // The sender ID transmitted in every voice packet: the local player's
        // machine_index (stable for the whole session, unique per connected
        // client), so receivers can map packets back to a name via
        // PlayerTable::get_player_by_rcon_id().
        std::uint32_t voice_sender_id() noexcept {
            auto *player = PlayerTable::get_player_table().get_client_player();
            if(player) return static_cast<std::uint32_t>(player->machine_index);
            return VOICE_NO_PLAYER_SENDER_ID;
        }

        std::uint32_t voice_timestamp() noexcept {
            return static_cast<std::uint32_t>(GetTickCount());
        }

        // A small persistent overlay in the top-left corner listing everyone
        // currently talking, redrawn every frame (Halo's on-screen text only
        // lasts one frame, so this must be re-applied continuously).
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

            // Receive first so remote speech remains responsive even while the
            // local push-to-talk key is not held.
            process_received_voice_packets();
            draw_voice_speaker_overlay();

            const bool talking = (GetAsyncKeyState(static_cast<int>(g_push_to_talk_key)) & 0x8000) != 0;
            if(talking) {
                // The capture thread and Opus work off the game thread. This
                // frame callback only moves already-produced packets to UDP.
                while(send_pending_voice_packet(voice_sender_id(), g_sequence, voice_timestamp())) {}
            }
            else {
                // Do not allow buffered microphone audio from before the PTT
                // press to be transmitted when the player starts talking.
                std::vector<std::int16_t> discarded;
                while(consume_voice_audio_packet(discarded)) {}
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

        // 32-bit FNV-1a. Not cryptographic - just needs to spread different
        // "host:port" strings across different room IDs so a shared relay can
        // tell unrelated Halo servers apart. Collisions would only mean two
        // different servers' voice chat briefly mixing, not a security issue.
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
            // Room 0 is reserved to mean "not connected to anything".
            return room_id == 0 ? 1 : room_id;
        }

        // Parses the argument of Halo's native "connect" command, which shows
        // up as either "connect <host>:<port>", "connect <host> <port>", or
        // just "connect <host>" (implying the default Halo port).
        bool parse_connect_target(const std::vector<std::string> &args, std::string &host, std::uint16_t &port) noexcept {
            if(args.empty() || args[0].empty()) return false;

            const auto colon = args[0].find(':');
            if(colon != std::string::npos) {
                host = args[0].substr(0, colon);
                if(!parse_port(args[0].c_str() + colon + 1, port)) return false;
                return !host.empty();
            }

            host = args[0];
            if(args.size() >= 2) {
                if(!parse_port(args[1].c_str(), port)) return false;
            }
            else {
                port = DEFAULT_HALO_PORT;
            }
            return true;
        }

        // Fires on every console command Chimera itself doesn't recognize -
        // i.e. Halo's own native commands, including "connect". This never
        // blocks anything (always returns true): it only watches for joining/
        // leaving a server to keep voice chat's room assignment and relay
        // target in sync, the same way a server browser's "connect" click
        // would.
        bool voice_watch_native_commands(const char *command) noexcept {
            auto args = split_arguments(command);
            if(args.empty()) return true;

            if(args[0] == "connect") {
                args.erase(args.begin());
                std::string host;
                std::uint16_t port = 0;
                if(parse_connect_target(args, host, port)) {
                    set_voice_chat_room(voice_room_id_for_server(host, port));

                    if(!g_manual_transport_override) {
                        // Default: same machine as the Halo server itself, on
                        // the voice-only port. Works automatically for any
                        // server whose admin also runs tools/voice_relay.py,
                        // with no single point of failure tied to one
                        // specific always-on box.
                        set_voice_chat_transport(host, DEFAULT_VOICE_PORT);
                    }
                    if(!voice_chat_enabled()) {
                        set_voice_chat_enabled(true);
                        update_voice_frame_registration();
                    }
                }
            }
            else if(args[0] == "disconnect") {
                set_voice_chat_room(0);
                    if(voice_chat_enabled()) {
                        set_voice_chat_enabled(false);
                        update_voice_frame_registration();
                    }
                }

            return true;
        }
    }

    void set_up_voice_native_command_watcher() noexcept {
        add_command_event(voice_watch_native_commands);
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
