// SPDX-License-Identifier: GPL-3.0-only

#include "voice_packet.hpp"
#include "voice_codec.hpp"

namespace Chimera {
    namespace {
        void write_u16(std::vector<std::uint8_t> &out, std::uint16_t value) {
            out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
        }

        void write_u32(std::vector<std::uint8_t> &out, std::uint32_t value) {
            out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
        }

        std::uint16_t read_u16(const std::uint8_t *in) {
            return static_cast<std::uint16_t>(in[0]) |
                   (static_cast<std::uint16_t>(in[1]) << 8u);
        }

        std::uint32_t read_u32(const std::uint8_t *in) {
            return static_cast<std::uint32_t>(in[0]) |
                   (static_cast<std::uint32_t>(in[1]) << 8u) |
                   (static_cast<std::uint32_t>(in[2]) << 16u) |
                   (static_cast<std::uint32_t>(in[3]) << 24u);
        }
    }

    bool build_voice_packet(std::uint32_t room_id,
                            std::uint32_t sender_id,
                            std::uint32_t sequence,
                            std::uint32_t timestamp,
                            const std::uint8_t *payload,
                            std::size_t payload_size,
                            std::vector<std::uint8_t> &packet) noexcept {
        packet.clear();
        if(!payload || payload_size == 0 || payload_size > VOICE_OPUS_MAX_PACKET_BYTES || payload_size > 0xFFFFu) return false;

        packet.reserve(VOICE_PACKET_HEADER_SIZE + payload_size);
        write_u32(packet, VOICE_PACKET_MAGIC);
        packet.push_back(VOICE_PACKET_VERSION);
        packet.push_back(0);
        write_u16(packet, static_cast<std::uint16_t>(payload_size));
        write_u32(packet, room_id);
        write_u32(packet, sender_id);
        write_u32(packet, sequence);
        write_u32(packet, timestamp);
        packet.insert(packet.end(), payload, payload + payload_size);
        return true;
    }

    bool parse_voice_packet(const std::uint8_t *packet,
                            std::size_t packet_size,
                            VoicePacketHeader &header,
                            const std::uint8_t *&payload) noexcept {
        payload = nullptr;
        if(!packet || packet_size < VOICE_PACKET_HEADER_SIZE) return false;

        header.magic = read_u32(packet + 0);
        header.version = packet[4];
        header.flags = packet[5];
        header.payload_size = read_u16(packet + 6);
        header.room_id = read_u32(packet + 8);
        header.sender_id = read_u32(packet + 12);
        header.sequence = read_u32(packet + 16);
        header.timestamp = read_u32(packet + 20);

        if(header.magic != VOICE_PACKET_MAGIC || header.version != VOICE_PACKET_VERSION) return false;
        if(header.payload_size == 0 || header.payload_size > VOICE_OPUS_MAX_PACKET_BYTES) return false;
        if(packet_size != VOICE_PACKET_HEADER_SIZE + static_cast<std::size_t>(header.payload_size)) return false;

        payload = packet + VOICE_PACKET_HEADER_SIZE;
        return true;
    }
}
