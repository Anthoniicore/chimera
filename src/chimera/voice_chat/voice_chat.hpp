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

    /**
     * Set which "room" this client's voice packets belong to. The relay
     * groups clients by room_id and never forwards audio between different
     * rooms, so a single always-on relay can safely serve many unrelated
     * Halo servers at once. Typically this is a hash of the Halo server's
     * connect address (see voice_room_id_for_server() in voice.cpp).
     *
     * A room_id of 0 means "not in a room" - packets are still accepted for
     * sending/receiving diagnostics, but process_received_voice_packets()
     * will discard anything that doesn't match our current room, and this
     * client won't transmit meaningfully to anyone until it is set.
     */
    void set_voice_chat_room(std::uint32_t room_id) noexcept;
    std::uint32_t voice_chat_room() noexcept;

    bool send_pending_voice_packet(std::uint32_t sender_id, std::uint32_t &sequence, std::uint32_t timestamp) noexcept;
    void process_received_voice_packets() noexcept;

    bool consume_encoded_voice_packet(std::vector<std::uint8_t> &packet) noexcept;
    bool consume_serialized_voice_packet(std::uint32_t sender_id, std::uint32_t &sequence, std::uint32_t timestamp, std::vector<std::uint8_t> &packet) noexcept;

    /**
     * Get the sender IDs of everyone whose voice packets have been received
     * within the last max_age_ms milliseconds (i.e. who is "currently talking").
     * @param  max_age_ms how recent a packet must be to count as active
     * @return             list of active sender IDs
     */
    std::vector<std::uint32_t> get_active_voice_speakers(std::uint32_t max_age_ms = 500) noexcept;

    /**
     * Diagnostic counters: total packets successfully sent/received since
     * voice chat was last enabled. Useful for figuring out where in the
     * capture -> encode -> send -> relay -> receive -> decode chain audio
     * is getting lost.
     */
    std::uint32_t voice_packets_sent_count() noexcept;
    std::uint32_t voice_packets_received_count() noexcept;
    std::uint32_t voice_packets_wrong_room_count() noexcept;
}
