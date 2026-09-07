// SPDX-License-Identifier: GPL-3.0-only

#include "audio_capture.hpp"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ksmedia.h>

// MinGW-w64's libuuid does not ship KSDATAFORMAT_SUBTYPE_PCM /
// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT (they normally come from ksuser.lib on
// the MSVC SDK). Define them here so this translation unit doesn't depend
// on a library that doesn't carry them under MinGW.
#include <initguid.h>
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_PCM,        0x00000001, 0x0000, 0x0010, 0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71);
DEFINE_GUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, 0x00000003, 0x0000, 0x0010, 0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71);

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>

namespace Chimera {
    namespace {
        constexpr std::size_t MAX_BUFFERED_SAMPLES = VOICE_AUDIO_SAMPLE_RATE * 10;

        std::atomic<bool> g_capture_running { false };
        std::atomic<bool> g_capture_stop_requested { false };
        std::thread g_capture_thread;
        std::mutex g_audio_mutex;
        std::deque<std::int16_t> g_audio_samples;

        bool format_is_pcm16(const WAVEFORMATEX *format) noexcept {
            if(format->wBitsPerSample != 16) {
                return false;
            }
            if(format->wFormatTag == WAVE_FORMAT_PCM) {
                return true;
            }
            if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
                const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
                return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM;
            }
            return false;
        }

        bool format_is_float32(const WAVEFORMATEX *format) noexcept {
            if(format->wBitsPerSample != 32) {
                return false;
            }
            if(format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
                return true;
            }
            if(format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
                const auto *extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(format);
                return extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
            }
            return false;
        }

        std::int16_t mono_sample_from_frame(const BYTE *data, UINT32 frame, const WAVEFORMATEX *format, bool pcm16, bool float32) noexcept {
            if(data == nullptr || format->nChannels == 0) {
                return 0;
            }

            double mixed = 0.0;
            const auto channels = static_cast<std::size_t>(format->nChannels);

            if(pcm16) {
                const auto *samples = reinterpret_cast<const std::int16_t *>(data) + static_cast<std::size_t>(frame) * channels;
                for(std::size_t channel = 0; channel < channels; channel++) {
                    mixed += samples[channel];
                }
                mixed /= static_cast<double>(channels);
            }
            else if(float32) {
                const auto *samples = reinterpret_cast<const float *>(data) + static_cast<std::size_t>(frame) * channels;
                for(std::size_t channel = 0; channel < channels; channel++) {
                    mixed += std::clamp(static_cast<double>(samples[channel]), -1.0, 1.0) * 32767.0;
                }
                mixed /= static_cast<double>(channels);
            }
            else {
                return 0;
            }

            mixed = std::clamp(mixed, -32768.0, 32767.0);
            return static_cast<std::int16_t>(std::lrint(mixed));
        }

        void append_normalized_samples(const BYTE *data, UINT32 frames, DWORD flags, const WAVEFORMATEX *format, std::uint64_t &resample_accumulator) noexcept {
            if(format == nullptr || format->nSamplesPerSec == 0 || frames == 0) {
                return;
            }

            const bool pcm16 = format_is_pcm16(format);
            const bool float32 = format_is_float32(format);
            if(!pcm16 && !float32) {
                return;
            }

            std::vector<std::int16_t> normalized;
            normalized.reserve(static_cast<std::size_t>(frames) * VOICE_AUDIO_SAMPLE_RATE / format->nSamplesPerSec + 4);

            const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
            for(UINT32 frame = 0; frame < frames; frame++) {
                const auto sample = silent ? static_cast<std::int16_t>(0) : mono_sample_from_frame(data, frame, format, pcm16, float32);

                resample_accumulator += VOICE_AUDIO_SAMPLE_RATE;
                while(resample_accumulator >= format->nSamplesPerSec) {
                    normalized.emplace_back(sample);
                    resample_accumulator -= format->nSamplesPerSec;
                }
            }

            if(normalized.empty()) {
                return;
            }

            std::lock_guard<std::mutex> lock(g_audio_mutex);
            const auto overflow = g_audio_samples.size() + normalized.size() > MAX_BUFFERED_SAMPLES
                ? g_audio_samples.size() + normalized.size() - MAX_BUFFERED_SAMPLES
                : 0;
            for(std::size_t i = 0; i < overflow && !g_audio_samples.empty(); i++) {
                g_audio_samples.pop_front();
            }
            g_audio_samples.insert(g_audio_samples.end(), normalized.begin(), normalized.end());
        }

        void capture_thread_main() noexcept {
            const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool com_initialized = SUCCEEDED(com_result);

            IMMDeviceEnumerator *enumerator = nullptr;
            IMMDevice *device = nullptr;
            IAudioClient *audio_client = nullptr;
            IAudioCaptureClient *capture_client = nullptr;
            WAVEFORMATEX *format = nullptr;
            std::uint64_t resample_accumulator = 0;

            do {
                if(!com_initialized || FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&enumerator)))) break;
                if(FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device))) break;
                if(FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&audio_client)))) break;
                if(FAILED(audio_client->GetMixFormat(&format))) break;
                if(FAILED(audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, format, nullptr))) break;
                if(FAILED(audio_client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&capture_client)))) break;
                if(FAILED(audio_client->Start())) break;

                g_capture_running.store(true, std::memory_order_release);
                while(!g_capture_stop_requested.load(std::memory_order_acquire)) {
                    UINT32 packet_length = 0;
                    if(FAILED(capture_client->GetNextPacketSize(&packet_length))) break;

                    while(packet_length != 0 && !g_capture_stop_requested.load(std::memory_order_acquire)) {
                        BYTE *data = nullptr;
                        UINT32 frames = 0;
                        DWORD flags = 0;
                        if(FAILED(capture_client->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                            packet_length = 0;
                            break;
                        }

                        append_normalized_samples(data, frames, flags, format, resample_accumulator);
                        capture_client->ReleaseBuffer(frames);
                        if(FAILED(capture_client->GetNextPacketSize(&packet_length))) packet_length = 0;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                audio_client->Stop();
            } while(false);

            if(format != nullptr) CoTaskMemFree(format);
            if(capture_client != nullptr) capture_client->Release();
            if(audio_client != nullptr) audio_client->Release();
            if(device != nullptr) device->Release();
            if(enumerator != nullptr) enumerator->Release();
            if(com_initialized) CoUninitialize();
            g_capture_running.store(false, std::memory_order_release);
        }
    }

    bool start_voice_audio_capture() noexcept {
        if(g_capture_running.load(std::memory_order_acquire) || g_capture_thread.joinable()) return true;
        g_capture_stop_requested.store(false, std::memory_order_release);
        try {
            g_capture_thread = std::thread(capture_thread_main);
            return true;
        }
        catch(...) {
            return false;
        }
    }

    void stop_voice_audio_capture() noexcept {
        g_capture_stop_requested.store(true, std::memory_order_release);
        if(g_capture_thread.joinable()) g_capture_thread.join();
        g_capture_running.store(false, std::memory_order_release);
    }

    bool voice_audio_capture_running() noexcept {
        return g_capture_running.load(std::memory_order_acquire);
    }

    std::size_t voice_audio_buffered_samples() noexcept {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        return g_audio_samples.size();
    }

    std::vector<std::int16_t> consume_voice_audio_samples(std::size_t maximum_samples) {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        const auto count = std::min(maximum_samples, g_audio_samples.size());
        std::vector<std::int16_t> output;
        output.reserve(count);
        for(std::size_t i = 0; i < count; i++) {
            output.emplace_back(g_audio_samples.front());
            g_audio_samples.pop_front();
        }
        return output;
    }

    bool consume_voice_audio_packet(std::vector<std::int16_t> &packet) {
        std::lock_guard<std::mutex> lock(g_audio_mutex);
        if(g_audio_samples.size() < VOICE_AUDIO_PACKET_SAMPLES) {
            return false;
        }

        packet.resize(VOICE_AUDIO_PACKET_SAMPLES);
        for(std::size_t i = 0; i < VOICE_AUDIO_PACKET_SAMPLES; i++) {
            packet[i] = g_audio_samples.front();
            g_audio_samples.pop_front();
        }
        return true;
    }
}
