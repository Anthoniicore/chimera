// SPDX-License-Identifier: GPL-3.0-only

#include "audio_playback.hpp"
#include "audio_capture.hpp"

#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <list>
#include <mutex>

namespace Chimera {
    namespace {
        HWAVEOUT g_wave = nullptr;
        std::mutex g_mutex;
        struct Buffer {
            WAVEHDR header{};
            std::vector<std::int16_t> samples;
        };
        // std::list, not std::vector: waveOutWrite() hands the OS/driver a
        // raw pointer to this Buffer's WAVEHDR (&buffer.header) which it
        // holds onto asynchronously until playback completes. A vector can
        // relocate *every* existing element on growth (emplace_back past
        // capacity) or shift *later* elements on erase() from the middle -
        // either one leaves the OS pointing at stale memory and corrupts
        // the heap the next time it marks a buffer done. A list never moves
        // an element's memory on insert/erase, so those pointers stay valid
        // for as long as the buffer is actually queued.
        std::list<Buffer> g_buffers;
    }

    bool initialize_voice_audio_playback() noexcept {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(g_wave) return true;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = static_cast<WORD>(VOICE_AUDIO_CHANNELS);
        format.nSamplesPerSec = VOICE_AUDIO_SAMPLE_RATE;
        format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        return waveOutOpen(&g_wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
    }

    void shutdown_voice_audio_playback() noexcept {
        std::lock_guard<std::mutex> lock(g_mutex);
        if(!g_wave) return;
        waveOutReset(g_wave);
        for(auto &buffer : g_buffers) {
            if(buffer.header.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wave, &buffer.header, sizeof(buffer.header));
        }
        g_buffers.clear();
        waveOutClose(g_wave);
        g_wave = nullptr;
    }

    bool voice_audio_playback_initialized() noexcept { return g_wave != nullptr; }

    bool queue_voice_audio_playback(const std::int16_t *pcm, std::size_t samples) noexcept {
        if(!pcm || samples == 0 || !initialize_voice_audio_playback()) return false;
        std::lock_guard<std::mutex> lock(g_mutex);
        if(g_buffers.size() >= 32) {
            for(auto it = g_buffers.begin(); it != g_buffers.end(); ++it) {
                if((it->header.dwFlags & WHDR_DONE) != 0) {
                    waveOutUnprepareHeader(g_wave, &it->header, sizeof(it->header));
                    g_buffers.erase(it);
                    break;
                }
            }
            if(g_buffers.size() >= 32) return false;
        }
        Buffer buffer{};
        buffer.samples.assign(pcm, pcm + samples);
        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
        buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(std::int16_t));
        if(waveOutPrepareHeader(g_wave, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR) return false;
        if(waveOutWrite(g_wave, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(g_wave, &buffer.header, sizeof(buffer.header));
            return false;
        }
        g_buffers.emplace_back(std::move(buffer));
        return true;
    }
}
