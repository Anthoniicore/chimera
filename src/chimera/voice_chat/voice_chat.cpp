// SPDX-License-Identifier: GPL-3.0-only

#include "voice_chat.hpp"
#include "audio_capture.hpp"
#include "audio_playback.hpp"
#include "voice_codec.hpp"
#include "voice_packet.hpp"
#include "voice_transport.hpp"

#include <chrono>
#include <unordered_map>

namespace Chimera {
    static bool g_voice_chat_initialized = false;
    static bool g_voice_chat_enabled = false;

    namespace {
        std::unordered_map<std::uint32_t, std::chrono::steady_clock::time_point> g_last_heard_from;
        std::uint32_t g_packets_sent = 0;
        std::uint32_t g_packets_received = 0;
        std::uint32_t g_packets_wrong_room = 0;
        std::uint32_t g_room_id = 0;
    }

    void set_voice_chat_room(std::uint32_t room_id) noexcept {
        if(room_id != g_room_id) {
            // Switching servers - old room's "who's talking" state no longer applies.
            g_last_heard_from.clear();
        }
        g_room_id = room_id;
    }
    std::uint32_t voice_chat_room() noexcept { return g_room_id; }

    void initialize_voice_chat() noexcept { if(!g_voice_chat_initialized) g_voice_chat_initialized = true; }

    void shutdown_voice_chat() noexcept {
        if(!g_voice_chat_initialized) return;
        g_voice_chat_enabled = false;
        stop_voice_audio_capture();
        shutdown_voice_audio_playback();
        shutdown_voice_codec();
        shutdown_voice_transport();
        g_voice_chat_initialized = false;
    }

    bool voice_chat_initialized() noexcept { return g_voice_chat_initialized; }
    bool voice_chat_enabled() noexcept { return g_voice_chat_initialized && g_voice_chat_enabled; }

    void set_voice_chat_enabled(bool enabled) noexcept {
        if(!g_voice_chat_initialized) initialize_voice_chat();
        if(enabled) {
            if(!initialize_voice_codec() || !start_voice_audio_capture() || !initialize_voice_audio_playback()) {
                stop_voice_audio_capture();
                shutdown_voice_audio_playback();
                shutdown_voice_codec();
                g_voice_chat_enabled = false;
                return;
            }
            g_voice_chat_enabled = true;
        } else {
            g_voice_chat_enabled = false;
            stop_voice_audio_capture();
        }
    }

    bool set_voice_chat_transport(const std::string &host, std::uint16_t port) noexcept {
        if(!g_voice_chat_initialized) initialize_voice_chat();
        return set_voice_transport_destination(host, port);
    }

    bool consume_encoded_voice_packet(std::vector<std::uint8_t> &packet) noexcept {
        packet.clear();
        if(!voice_chat_enabled()) return false;
        std::vector<std::int16_t> pcm;
        if(!consume_voice_audio_packet(pcm)) return false;
        return encode_voice_audio_packet(pcm.data(), pcm.size(), packet);
    }

    bool consume_serialized_voice_packet(std::uint32_t sender_id, std::uint32_t &sequence, std::uint32_t timestamp, std::vector<std::uint8_t> &packet) noexcept {
        std::vector<std::uint8_t> opus_packet;
        if(!consume_encoded_voice_packet(opus_packet)) return false;
        if(!build_voice_packet(g_room_id, sender_id, sequence, timestamp, opus_packet.data(), opus_packet.size(), packet)) return false;
        ++sequence;
        return true;
    }

    bool send_pending_voice_packet(std::uint32_t sender_id, std::uint32_t &sequence, std::uint32_t timestamp) noexcept {
        if(!voice_chat_enabled() || !voice_transport_has_destination() || g_room_id == 0) return false;
        std::vector<std::uint8_t> packet;
        const auto next_sequence = sequence;
        if(!consume_serialized_voice_packet(sender_id, sequence, timestamp, packet)) return false;
        if(send_voice_transport_packet(packet.data(), packet.size())) {
            ++g_packets_sent;
            return true;
        }
        sequence = next_sequence;
        return false;
    }

    void process_received_voice_packets() noexcept {
        if(!voice_chat_enabled()) return;
        for(;;) {
            std::vector<std::uint8_t> packet;
            if(!receive_voice_transport_packet(packet)) break;
            VoicePacketHeader header{};
            const std::uint8_t *payload = nullptr;
            if(!parse_voice_packet(packet.data(), packet.size(), header, payload)) continue;
            if(header.room_id != g_room_id) {
                ++g_packets_wrong_room;
                continue;
            }
            std::vector<std::int16_t> pcm;
            if(!decode_voice_audio_packet(payload, header.payload_size, pcm)) continue;
            queue_voice_audio_playback(pcm.data(), pcm.size());
            g_last_heard_from[header.sender_id] = std::chrono::steady_clock::now();
            ++g_packets_received;
        }
    }

    std::uint32_t voice_packets_sent_count() noexcept { return g_packets_sent; }
    std::uint32_t voice_packets_received_count() noexcept { return g_packets_received; }
    std::uint32_t voice_packets_wrong_room_count() noexcept { return g_packets_wrong_room; }

    std::vector<std::uint32_t> get_active_voice_speakers(std::uint32_t max_age_ms) noexcept {
        std::vector<std::uint32_t> speakers;
        const auto now = std::chrono::steady_clock::now();
        for(auto &entry : g_last_heard_from) {
            const auto age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.second).count();
            if(age_ms >= 0 && static_cast<std::uint32_t>(age_ms) <= max_age_ms) {
                speakers.push_back(entry.first);
            }
        }
        return speakers;
    }

    namespace {
        struct VoiceChatLifecycle {
            VoiceChatLifecycle() noexcept { initialize_voice_chat(); }
            ~VoiceChatLifecycle() { shutdown_voice_chat(); }
        };
        static VoiceChatLifecycle g_voice_chat_lifecycle;
    }
}
