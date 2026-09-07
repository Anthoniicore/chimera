// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Chimera {
    // Voice capture always exposes normalized audio to later stages:
    // 48 kHz, mono, signed 16-bit PCM.
    constexpr std::uint32_t VOICE_AUDIO_SAMPLE_RATE = 48000;
    constexpr std::uint16_t VOICE_AUDIO_CHANNELS = 1;

    // Fixed voice frames are intentionally aligned with the standard Opus
    // real-time packet duration used by the next stage of the pipeline.
    constexpr std::uint32_t VOICE_AUDIO_PACKET_DURATION_MS = 20;
    constexpr std::size_t VOICE_AUDIO_PACKET_SAMPLES =
        static_cast<std::size_t>(VOICE_AUDIO_SAMPLE_RATE) * VOICE_AUDIO_PACKET_DURATION_MS / 1000;

    bool start_voice_audio_capture() noexcept;
    void stop_voice_audio_capture() noexcept;
    bool voice_audio_capture_running() noexcept;

    // Audio currently buffered in normalized 48 kHz mono PCM16 samples.
    std::size_t voice_audio_buffered_samples() noexcept;
    std::vector<std::int16_t> consume_voice_audio_samples(std::size_t maximum_samples);

    // Returns one complete fixed-duration voice frame. Partial frames are never
    // consumed so the encoder stage cannot accidentally receive malformed input.
    bool consume_voice_audio_packet(std::vector<std::int16_t> &packet);
}
