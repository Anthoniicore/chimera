// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Chimera {
    constexpr std::uint32_t VOICE_PACKET_MAGIC = 0x56434831u; // "VCH1"
    constexpr std::uint8_t VOICE_PACKET_VERSION = 2;
    constexpr std::size_t VOICE_PACKET_HEADER_SIZE = 24;

    // Set on packets that exist only to keep a client's own NAT mapping
    // (and the relay's per-room timeout) alive during silence. Carries a
    // single dummy payload byte - it is never decoded as audio and never
    // counts as "this player is talking".
    constexpr std::uint8_t VOICE_PACKET_FLAG_KEEPALIVE = 0x01;

    struct VoicePacketHeader {
        std::uint32_t magic;
        std::uint8_t version;
        std::uint8_t flags;
        std::uint16_t payload_size;
        std::uint32_t room_id;
        std::uint32_t sender_id;
        std::uint32_t sequence;
        std::uint32_t timestamp;
    };

    bool build_voice_packet(std::uint32_t room_id,
                            std::uint32_t sender_id,
                            std::uint32_t sequence,
                            std::uint32_t timestamp,
                            const std::uint8_t *payload,
                            std::size_t payload_size,
                            std::vector<std::uint8_t> &packet) noexcept;

    // Builds a minimal packet with VOICE_PACKET_FLAG_KEEPALIVE set and a
    // single dummy payload byte. Sent periodically while connected but not
    // talking, purely to keep this client's NAT mapping (and the relay's
    // per-room timeout) from expiring during silence.
    bool build_voice_keepalive_packet(std::uint32_t room_id,
                                       std::uint32_t sender_id,
                                       std::vector<std::uint8_t> &packet) noexcept;

    // Validates and decodes a packet without taking ownership of its payload.
    bool parse_voice_packet(const std::uint8_t *packet,
                            std::size_t packet_size,
                            VoicePacketHeader &header,
                            const std::uint8_t *&payload) noexcept;
}
