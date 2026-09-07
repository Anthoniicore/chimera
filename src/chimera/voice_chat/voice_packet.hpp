// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Chimera {
    constexpr std::uint32_t VOICE_PACKET_MAGIC = 0x56434831u; // "VCH1"
    constexpr std::uint8_t VOICE_PACKET_VERSION = 2;
    constexpr std::size_t VOICE_PACKET_HEADER_SIZE = 24;

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

    // Serializes a self-contained voice packet. The wire format is explicitly
    // byte-oriented and does not depend on compiler struct packing.
    //
    // room_id identifies which Halo server this packet belongs to (a hash of
    // that server's connect address) so a shared relay can keep voice chat
    // from bleeding across unrelated games; it is not a secret.
    bool build_voice_packet(std::uint32_t room_id,
                            std::uint32_t sender_id,
                            std::uint32_t sequence,
                            std::uint32_t timestamp,
                            const std::uint8_t *payload,
                            std::size_t payload_size,
                            std::vector<std::uint8_t> &packet) noexcept;

    // Validates and decodes a packet without taking ownership of its payload.
    bool parse_voice_packet(const std::uint8_t *packet,
                            std::size_t packet_size,
                            VoicePacketHeader &header,
                            const std::uint8_t *&payload) noexcept;
}
