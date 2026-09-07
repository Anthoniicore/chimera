// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Chimera {
    constexpr std::size_t VOICE_OPUS_FRAME_SAMPLES = 960;
    constexpr std::size_t VOICE_OPUS_MAX_PACKET_BYTES = 1275;

    bool initialize_voice_codec() noexcept;
    void shutdown_voice_codec() noexcept;
    bool voice_codec_initialized() noexcept;

    bool encode_voice_audio_packet(const std::int16_t *pcm,
                                   std::size_t samples,
                                   std::vector<std::uint8_t> &packet) noexcept;

    bool decode_voice_audio_packet(const std::uint8_t *packet,
                                   std::size_t packet_size,
                                   std::vector<std::int16_t> &pcm) noexcept;
}
