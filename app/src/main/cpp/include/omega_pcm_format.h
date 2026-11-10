// omega_pcm_format.h — Conversión multi-formato PCM sin allocs (AudioFlinger).
//
// Extraído de omega_effect.cpp (v2.5.384) para que la lógica de formato sea
// testeable en host. Los helpers normalizan a float [-1, 1] en la entrada y
// re-empaquetan al formato exacto de salida.
//
// REGRESIÓN v2.5.382 (sin audio + tronidos): el default de creación del
// efecto era PCM_16_BIT (0x01). Pero AudioFlinger entrega el buffer del
// efecto en FLOAT (el mixer mezcla en float); el PCM16 que mide la sonda PAL
// es DESPUÉS del mixer (salida del HAL), no en el efecto. Con PCM16 por
// defecto, frameBytes era 4 en vez de 8 (solo se copiaba la mitad del buffer),
// el deinterleave leía float como int16 (basura) y el write escribía int16 en
// un buffer float (basura) → SIN AUDIO + TRONIDOS al cambiar de app. La
// v2.5.380 (config en ceros → formato 0 → trato float) sonaba limpia, lo que
// prueba que el buffer real es float. Por eso el default ES PCM_FLOAT.
// Si SET_CONFIG llega, sobreescribe ctx->config con el formato real del HAL.
//
// Formatos (audio_effect_compat.h): PCM_16_BIT 0x01, PCM_32_BIT 0x03,
// PCM_8_24_BIT 0x04, PCM_FLOAT 0x05, PCM_24_BIT_PACKED 0x06.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace ivanna {

// Formato por defecto del efecto cuando EFFECT_CMD_SET_CONFIG jamás llega
// (este HAL Qualcomm no lo entrega). DEBE ser PCM_FLOAT: es el formato del
// buffer del mixer de AudioFlinger. Cambiarlo a PCM_16_BIT reintroduce la
// regresión de la v2.5.382 (sin audio + tronidos).
inline constexpr uint8_t kOmegaDefaultFormat = 0x05u;  // AUDIO_FORMAT_PCM_FLOAT
inline constexpr uint32_t kOmegaDefaultSampleRate = 48000u;

inline size_t omega_bytes_per_sample(uint8_t fmt) noexcept {
    switch (fmt) {
        case 0x01u: return 2u;  // AUDIO_FORMAT_PCM_16_BIT
        case 0x06u: return 3u;  // AUDIO_FORMAT_PCM_24_BIT_PACKED
        default:    return 4u;  // PCM_FLOAT (0x05), PCM_32_BIT (0x03), PCM_8_24_BIT (0x04), 0 (default float)
    }
}

inline void omega_deinterleave_chunk(const void* rawIn, uint8_t fmt,
                                     int frameOffset, int chunkFrames,
                                     float* dstL, float* dstR) noexcept {
    if (!rawIn || chunkFrames <= 0) return;
    switch (fmt) {
        case 0x01u: {  // AUDIO_FORMAT_PCM_16_BIT (S16_LE)
            const int16_t* s16 = static_cast<const int16_t*>(rawIn) + (size_t)frameOffset * 2u;
            constexpr float kScale16 = 1.0f / 32768.0f;
            for (int n = 0; n < chunkFrames; ++n) {
                dstL[n] = static_cast<float>(s16[2 * n]) * kScale16;
                dstR[n] = static_cast<float>(s16[2 * n + 1]) * kScale16;
            }
            break;
        }
        case 0x03u: {  // AUDIO_FORMAT_PCM_32_BIT (S32_LE)
            const int32_t* s32 = static_cast<const int32_t*>(rawIn) + (size_t)frameOffset * 2u;
            constexpr float kScale32 = 1.0f / 2147483648.0f;
            for (int n = 0; n < chunkFrames; ++n) {
                dstL[n] = static_cast<float>(s32[2 * n]) * kScale32;
                dstR[n] = static_cast<float>(s32[2 * n + 1]) * kScale32;
            }
            break;
        }
        case 0x04u: {  // AUDIO_FORMAT_PCM_8_24_BIT (Q8.23 en int32_t)
            const int32_t* q24 = static_cast<const int32_t*>(rawIn) + (size_t)frameOffset * 2u;
            constexpr float kScale24 = 1.0f / 8388608.0f;
            for (int n = 0; n < chunkFrames; ++n) {
                dstL[n] = std::clamp(static_cast<float>(q24[2 * n]) * kScale24, -1.0f, 1.0f);
                dstR[n] = std::clamp(static_cast<float>(q24[2 * n + 1]) * kScale24, -1.0f, 1.0f);
            }
            break;
        }
        case 0x06u: {  // AUDIO_FORMAT_PCM_24_BIT_PACKED (3B LE con signo)
            const uint8_t* p = static_cast<const uint8_t*>(rawIn) + (size_t)frameOffset * 6u;
            constexpr float kScale24 = 1.0f / 8388608.0f;
            for (int n = 0; n < chunkFrames; ++n) {
                const int32_t iL = (static_cast<int32_t>(static_cast<int8_t>(p[2])) << 16) |
                                   (static_cast<int32_t>(p[1]) << 8) |
                                    static_cast<int32_t>(p[0]);
                const int32_t iR = (static_cast<int32_t>(static_cast<int8_t>(p[5])) << 16) |
                                   (static_cast<int32_t>(p[4]) << 8) |
                                    static_cast<int32_t>(p[3]);
                dstL[n] = static_cast<float>(iL) * kScale24;
                dstR[n] = static_cast<float>(iR) * kScale24;
                p += 6;
            }
            break;
        }
        default: {  // AUDIO_FORMAT_PCM_FLOAT (0x05) o 0 (default en tests)
            const float* f32 = static_cast<const float*>(rawIn) + (size_t)frameOffset * 2u;
            for (int n = 0; n < chunkFrames; ++n) {
                const float l = f32[2 * n];
                const float r = f32[2 * n + 1];
                dstL[n] = std::isfinite(l) ? l : 0.0f;
                dstR[n] = std::isfinite(r) ? r : 0.0f;
            }
            break;
        }
    }
}

inline void omega_write_output_frame(void* rawOut, uint8_t fmt,
                                     int frameIdx, float l, float r) noexcept {
    const float cL = std::isfinite(l) ? std::clamp(l, -1.0f, 1.0f) : 0.0f;
    const float cR = std::isfinite(r) ? std::clamp(r, -1.0f, 1.0f) : 0.0f;
    switch (fmt) {
        case 0x01u: {  // AUDIO_FORMAT_PCM_16_BIT
            int16_t* s16 = static_cast<int16_t*>(rawOut) + (size_t)frameIdx * 2u;
            s16[0] = static_cast<int16_t>(std::lrintf(cL * 32767.0f));
            s16[1] = static_cast<int16_t>(std::lrintf(cR * 32767.0f));
            break;
        }
        case 0x03u: {  // AUDIO_FORMAT_PCM_32_BIT
            int32_t* s32 = static_cast<int32_t*>(rawOut) + (size_t)frameIdx * 2u;
            s32[0] = static_cast<int32_t>(cL * 2147483392.0f);
            s32[1] = static_cast<int32_t>(cR * 2147483392.0f);
            break;
        }
        case 0x04u: {  // AUDIO_FORMAT_PCM_8_24_BIT (Q8.23)
            int32_t* q24 = static_cast<int32_t*>(rawOut) + (size_t)frameIdx * 2u;
            q24[0] = static_cast<int32_t>(std::lrintf(cL * 8388607.0f));
            q24[1] = static_cast<int32_t>(std::lrintf(cR * 8388607.0f));
            break;
        }
        case 0x06u: {  // AUDIO_FORMAT_PCM_24_BIT_PACKED (3B LE)
            uint8_t* p = static_cast<uint8_t*>(rawOut) + (size_t)frameIdx * 6u;
            const uint32_t uL = static_cast<uint32_t>(static_cast<int32_t>(std::lrintf(cL * 8388607.0f)));
            const uint32_t uR = static_cast<uint32_t>(static_cast<int32_t>(std::lrintf(cR * 8388607.0f)));
            p[0] = static_cast<uint8_t>(uL & 0xFFu);
            p[1] = static_cast<uint8_t>((uL >> 8) & 0xFFu);
            p[2] = static_cast<uint8_t>((uL >> 16) & 0xFFu);
            p[3] = static_cast<uint8_t>(uR & 0xFFu);
            p[4] = static_cast<uint8_t>((uR >> 8) & 0xFFu);
            p[5] = static_cast<uint8_t>((uR >> 16) & 0xFFu);
            break;
        }
        default: {  // AUDIO_FORMAT_PCM_FLOAT (0x05) o 0
            float* f32 = static_cast<float*>(rawOut) + (size_t)frameIdx * 2u;
            f32[0] = std::isfinite(l) ? std::clamp(l, -1.0f, 1.0f) : 0.0f;
            f32[1] = std::isfinite(r) ? std::clamp(r, -1.0f, 1.0f) : 0.0f;
            break;
        }
    }
}

}  // namespace ivanna
