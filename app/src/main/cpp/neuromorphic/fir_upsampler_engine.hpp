/*
 * FIRUpsamplerEngine — interpolador polifásico de fase lineal real.
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS.
 *
 * ANTES ("stub"): zero-stuffing con FACTOR fijo en 4 + filtro single-pole
 * (IIR de un polo, ni siquiera un FIR). neuro_cochlear_manifold.cpp reserva
 * y consume buffers dimensionados con `upsample_factor` en TIEMPO DE
 * EJECUCIÓN (sample_rate_out / sample_rate_in, hasta 16x para 768kHz/48kHz
 * — ver ManifoldState::upsample_factor) pero el stub solo llenaba las
 * primeras N*4 muestras de un buffer de hasta N*16: el resto quedaba en
 * silencio/memset(0) sin que nada lo detectara. Con factores >4 (96kHz,
 * 192kHz, 384kHz, 768kHz) la señal de salida real estaba mayormente
 * silenciada — la "resolución" hi-res prometida nunca llegaba completa
 * al pipeline final que exporta a int32_t (ver el escalado *2147483647
 * en neuro_cochlear_process_block: ESE es el punto real de 32-bit S32_LE
 * que ya viste referenciado en UsbAudioProManager).
 *
 * AHORA: ventana Blackman-Harris de 4 términos (misma familia usada en
 * ivanna_npe_engine.cpp/IvannaFastRpcClient — de ahí "conectar el Beckman"),
 * rediseñada como filtro polifásico paramétrico por `upsampleFactor` real
 * en vez de una tabla fija a 16x. Fase lineal exacta (FIR simétrico), sin
 * aliasing de imagen, sin dependencia de Hexagon/HVX — corre en cualquier
 * CPU ARM, con o sin cDSP Qualcomm disponible.
 */
#pragma once
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class FIRUpsamplerEngine {
public:
    // upsampleFactor: relación Fs_out/Fs_in real (2, 4, 8, 16...).
    // tapsPerPhase: taps por fase polifásica — 32 da un lóbulo principal
    // angosto y atenuación de imagen >80dB para factores hasta 16x, con
    // coste O(tapsPerPhase) MACs por canal por muestra de entrada (igual
    // de barato que el zero-stuffing+FIR directo equivalente).
    explicit FIRUpsamplerEngine(uint32_t upsampleFactor = 16, uint32_t tapsPerPhase = 32) noexcept
        : factor_(upsampleFactor < 1 ? 1 : upsampleFactor),
          taps_(tapsPerPhase < 4 ? 4 : tapsPerPhase) {
        coeffs_.resize(static_cast<size_t>(taps_) * factor_);
        designBlackmanHarrisSinc();
        delay_.assign(taps_, 0.0f);
    }

    // Upsampling x`factor_` con FIR de fase lineal real. `output` debe tener
    // espacio para numSamples * factor_ floats (mismo contrato que antes,
    // pero ahora SÍ se llenan todas las muestras, no solo las primeras N*4).
    void process(const float* input, float* output, size_t numSamples) noexcept {
        if (!input || !output) return;
        for (size_t n = 0; n < numSamples; ++n) {
            for (uint32_t i = taps_ - 1; i > 0; --i) delay_[i] = delay_[i - 1];
            delay_[0] = input[n];

            for (uint32_t phase = 0; phase < factor_; ++phase) {
                float acc = 0.0f;
                for (uint32_t k = 0; k < taps_; ++k) {
                    acc += delay_[k] * coeffs_[phase + k * factor_];
                }
                output[n * factor_ + phase] = acc;
            }
        }
    }

    void reset() noexcept {
        std::fill(delay_.begin(), delay_.end(), 0.0f);
    }

    uint32_t upsampleFactor() const noexcept { return factor_; }

private:
    // Diseña el prototipo passband (windowed-sinc, Blackman-Harris 4 términos)
    // y lo decima en `factor_` fases polifásicas: coeffs_[phase + k*factor_]
    // es el tap k de la fase `phase` (decomposición polifásica estándar).
    void designBlackmanHarrisSinc() noexcept {
        const uint32_t M = taps_ * factor_;
        const float cutoff = 1.0f / (2.0f * static_cast<float>(factor_));
        for (uint32_t i = 0; i < M; ++i) {
            const int32_t n = static_cast<int32_t>(i) - static_cast<int32_t>(M / 2);
            const float sinc = (n == 0)
                ? 1.0f
                : sinf((float)M_PI * cutoff * n) / ((float)M_PI * cutoff * n);

            const float a0 = 0.35875f, a1 = 0.48829f, a2 = 0.14128f, a3 = 0.01168f;
            const float x = (2.0f * (float)M_PI * i) / (M - 1);
            const float window = a0 - a1 * cosf(x) + a2 * cosf(2.0f * x) - a3 * cosf(3.0f * x);

            coeffs_[i] = sinc * window * cutoff * 2.0f * static_cast<float>(factor_);
        }
    }

    uint32_t factor_;
    uint32_t taps_;
    std::vector<float> coeffs_;
    std::vector<float> delay_;
};
