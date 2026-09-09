// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Chimera {
    void initialize_voice_chat() noexcept;
    void shutdown_voice_chat() noexcept;
    bool voice_chat_initialized() noexcept;
    bool voice_chat_enabled() noexcept;
    void set_voice_chat_enabled(bool enabled) noexcept;

    bool set_voice_chat_transport(const std::string &host, std::uint16_t port) noexcept;

    void set_voice_chat_room(std::uint32_t room_id) noexcept;
    std::uint32_t voice_chat_room() noexcept;

    bool send_pending_voice_packet(std::uint32_t sender_id, std::uint32_t &sequence, std::uint32_t timestamp) noexcept;

    /**
     * Send a single keepalive packet (see VOICE_PACKET_FLAG_KEEPALIVE) if
     * connected to a room and a relay destination is set. Meant to be
     * called periodically (every ~20s) while voice chat is enabled but the
     * player isn't currently talking, to keep this client's own NAT mapping
     * -and- the relay's per-room timeout from expiring during silence. Does
     * nothing and returns false if not connected to a server.
     */
    bool send_voice_keepalive_packet(std::uint32_t sender_id) noexcept;

    void process_received_voice_packets() noexcept;

    bool consume_encoded_voice_packet(std::vector<std::uint8_t> &packet) noexcept;
    bool consume_serialized_voice_packet(std::uint32_t sender_id, std::uint32_t &sequence, std::uint32_t timestamp, std::vector<std::uint8_t> &packet) noexcept;

    std::vector<std::uint32_t> get_active_voice_speakers(std::uint32_t max_age_ms = 500) noexcept;

    std::uint32_t voice_packets_sent_count() noexcept;
    std::uint32_t voice_packets_received_count() noexcept;
    std::uint32_t voice_packets_wrong_room_count() noexcept;
}
