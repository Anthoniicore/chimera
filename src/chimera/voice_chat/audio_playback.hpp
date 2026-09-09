// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstddef>
#include <cstdint>

namespace Chimera {
    bool initialize_voice_audio_playback() noexcept;
    void shutdown_voice_audio_playback() noexcept;
    bool voice_audio_playback_initialized() noexcept;
    bool queue_voice_audio_playback(const std::int16_t *pcm, std::size_t samples) noexcept;
    float get_voice_audio_playback_volume() noexcept;
    void set_voice_audio_playback_volume(float volume) noexcept;
}
