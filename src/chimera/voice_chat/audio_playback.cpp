// SPDX-License-Identifier: GPL-3.0-only

#include "audio_playback.hpp"
#include "audio_capture.hpp"

#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <list>
#include <mutex>
#include <algorithm>
#include <cstdint>

namespace Chimera {
    namespace {
        HWAVEOUT g_wave = nullptr;
        std::mutex g_mutex;

        // Default voice playback volume.
        // 1.0f = original volume
        // 1.5f = 50% louder
        // 2.0f = 100% louder
        float g_voice_volume = 1.5f;

        struct Buffer {
            WAVEHDR header{};
            std::vector<std::int16_t> samples;
        };

        std::list<Buffer> g_buffers;

        void apply_voice_volume(std::vector<std::int16_t> &samples) noexcept {
            for(auto &sample : samples) {
                const float amplified =
                    static_cast<float>(sample) * g_voice_volume;

                if(amplified > 32767.0f) {
                    sample = 32767;
                } else if(amplified < -32768.0f) {
                    sample = -32768;
                } else {
                    sample = static_cast<std::int16_t>(amplified);
                }
            }
        }
    }

    bool initialize_voice_audio_playback() noexcept {
        std::lock_guard<std::mutex> lock(g_mutex);

        if(g_wave) {
            return true;
        }

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(VOICE_AUDIO_CHANNELS);
        format.nSamplesPerSec = VOICE_AUDIO_SAMPLE_RATE;
        format.wBitsPerSample = 16;
        format.nBlockAlign =
            static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec =
            format.nSamplesPerSec * format.nBlockAlign;

        return waveOutOpen(
            &g_wave,
            WAVE_MAPPER,
            &format,
            0,
            0,
            CALLBACK_NULL
        ) == MMSYSERR_NOERROR;
    }

    void shutdown_voice_audio_playback() noexcept {
        std::lock_guard<std::mutex> lock(g_mutex);

        if(!g_wave) {
            return;
        }

        waveOutReset(g_wave);

        for(auto &buffer : g_buffers) {
            if(buffer.header.dwFlags & WHDR_PREPARED) {
                waveOutUnprepareHeader(
                    g_wave,
                    &buffer.header,
                    sizeof(buffer.header)
                );
            }
        }

        g_buffers.clear();

        waveOutClose(g_wave);
        g_wave = nullptr;
    }

    bool voice_audio_playback_initialized() noexcept {
        return g_wave != nullptr;
    }

    bool queue_voice_audio_playback(
        const std::int16_t *pcm,
        std::size_t samples
    ) noexcept {
        if(!pcm || samples == 0) {
            return false;
        }

        if(!initialize_voice_audio_playback()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_mutex);

        if(g_buffers.size() >= 32) {
            for(auto it = g_buffers.begin(); it != g_buffers.end(); ++it) {
                if((it->header.dwFlags & WHDR_DONE) != 0) {
                    waveOutUnprepareHeader(
                        g_wave,
                        &it->header,
                        sizeof(it->header)
                    );

                    g_buffers.erase(it);
                    break;
                }
            }

            if(g_buffers.size() >= 32) {
                return false;
            }
        }

        g_buffers.emplace_back();

        auto it = g_buffers.end();
        --it;

        Buffer &buffer = *it;

        buffer.samples.assign(pcm, pcm + samples);

        // Apply playback gain before sending samples to WinMM.
        apply_voice_volume(buffer.samples);

        buffer.header.lpData =
            reinterpret_cast<LPSTR>(buffer.samples.data());

        buffer.header.dwBufferLength =
            static_cast<DWORD>(
                buffer.samples.size() * sizeof(std::int16_t)
            );

        if(waveOutPrepareHeader(
            g_wave,
            &buffer.header,
            sizeof(buffer.header)
        ) != MMSYSERR_NOERROR) {
            g_buffers.erase(it);
            return false;
        }

        if(waveOutWrite(
            g_wave,
            &buffer.header,
            sizeof(buffer.header)
        ) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(
                g_wave,
                &buffer.header,
                sizeof(buffer.header)
            );

            g_buffers.erase(it);
            return false;
        }

        return true;
    }

    float get_voice_audio_playback_volume() noexcept {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_voice_volume;
    }

    void set_voice_audio_playback_volume(float volume) noexcept {
        std::lock_guard<std::mutex> lock(g_mutex);

        // Keep the value in a safe and useful range.
        volume = std::max(0.0f, std::min(volume, 4.0f));

        g_voice_volume = volume;
    }
}
