// SPDX-License-Identifier: GPL-3.0-only

#include "voice_codec.hpp"
#include "audio_capture.hpp"
#include <opus.h>

namespace Chimera {
    namespace {
        OpusEncoder *g_encoder = nullptr;
        OpusDecoder *g_decoder = nullptr;
    }

    bool initialize_voice_codec() noexcept {
        if(g_encoder && g_decoder) return true;
        int error = OPUS_OK;
        if(!g_encoder) {
            auto *encoder = opus_encoder_create(VOICE_AUDIO_SAMPLE_RATE, VOICE_AUDIO_CHANNELS, OPUS_APPLICATION_VOIP, &error);
            if(!encoder || error != OPUS_OK) {
                if(encoder) opus_encoder_destroy(encoder);
                return false;
            }
            opus_encoder_ctl(encoder, OPUS_SET_BITRATE(32000));
            opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(5));
            opus_encoder_ctl(encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
            g_encoder = encoder;
        }
        if(!g_decoder) {
            auto *decoder = opus_decoder_create(VOICE_AUDIO_SAMPLE_RATE, VOICE_AUDIO_CHANNELS, &error);
            if(!decoder || error != OPUS_OK) {
                if(decoder) opus_decoder_destroy(decoder);
                return false;
            }
            g_decoder = decoder;
        }
        return true;
    }

    void shutdown_voice_codec() noexcept {
        if(g_encoder) { opus_encoder_destroy(g_encoder); g_encoder = nullptr; }
        if(g_decoder) { opus_decoder_destroy(g_decoder); g_decoder = nullptr; }
    }

    bool voice_codec_initialized() noexcept { return g_encoder != nullptr && g_decoder != nullptr; }

    bool encode_voice_audio_packet(const std::int16_t *pcm, std::size_t samples, std::vector<std::uint8_t> &packet) noexcept {
        packet.clear();
        if(!pcm || samples != VOICE_OPUS_FRAME_SAMPLES || !initialize_voice_codec()) return false;
        packet.resize(VOICE_OPUS_MAX_PACKET_BYTES);
        const int encoded = opus_encode(g_encoder, reinterpret_cast<const opus_int16 *>(pcm), static_cast<int>(samples), reinterpret_cast<unsigned char *>(packet.data()), static_cast<opus_int32>(packet.size()));
        if(encoded < 0) { packet.clear(); return false; }
        packet.resize(static_cast<std::size_t>(encoded));
        return true;
    }

    bool decode_voice_audio_packet(const std::uint8_t *packet, std::size_t packet_size, std::vector<std::int16_t> &pcm) noexcept {
        pcm.clear();
        if(!packet || packet_size == 0 || packet_size > VOICE_OPUS_MAX_PACKET_BYTES || !initialize_voice_codec()) return false;
        pcm.resize(VOICE_OPUS_FRAME_SAMPLES * VOICE_AUDIO_CHANNELS);
        const int decoded = opus_decode(g_decoder,
                                        reinterpret_cast<const unsigned char *>(packet),
                                        static_cast<opus_int32>(packet_size),
                                        reinterpret_cast<opus_int16 *>(pcm.data()),
                                        static_cast<int>(VOICE_OPUS_FRAME_SAMPLES),
                                        0);
        if(decoded <= 0) { pcm.clear(); return false; }
        pcm.resize(static_cast<std::size_t>(decoded) * VOICE_AUDIO_CHANNELS);
        return true;
    }
}
