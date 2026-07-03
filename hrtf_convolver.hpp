// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — HRTFConvolver
 *
 * Convolución binaural en tiempo real vía FFT (overlap-save),
 * con caché de HRIR por ángulo cuantizado y crossfade de bloque
 * completo cuando la posición cambia — evita "zipper noise"
 * (clics/escalones) que aparecerían si se cambiara el filtro a
 * mitad de señal sin transición.
 *
 * Bloque de proceso interno: BLOCK muestras. Tamaño FFT: next
 * pow2(BLOCK + IR_LEN - 1). Overlap-save clásico: se descartan
 * las primeras (IR_LEN-1) muestras de cada IFFT (aliasing
 * circular) y se toman las BLOCK muestras finales como válidas.
 *
 * Entrada de tamaño arbitrario (n variable por callback de audio)
 * se acumula en un buffer circular y se procesa en sub-bloques de
 * BLOCK muestras — el resto queda para el siguiente process().
 * ============================================================
 */

#include "fft_radix2.hpp"
#include "synthetic_hrtf.hpp"
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>

namespace ivanna {

class HRTFConvolver {
public:
    static constexpr int BLOCK  = 256;   // muestras procesadas por sub-bloque
    static constexpr int IR_LEN = 128;   // taps de la HRIR sintética

    void init(uint32_t sampleRate) {
        sr_ = sampleRate;
        hrtf_.init(sampleRate, IR_LEN);

        fftSize_ = next_pow2(BLOCK + IR_LEN - 1);
        fft_ = std::make_unique<FFTRadix2>(fftSize_);

        histL_.assign(fftSize_, 0.f);
        histR_.assign(fftSize_, 0.f);
        pendingIn_L_.reserve(BLOCK * 2);
        pendingIn_R_.reserve(BLOCK * 2);
        outQueue_L_.reserve(BLOCK * 4);
        outQueue_R_.reserve(BLOCK * 4);

        set_position(0.f, 0.5f); // frente, agresividad media por defecto
        // Fuerza a que el primer bloque no haga crossfade (no hay "anterior" real)
        crossfadeActive_ = false;
    }

    void reset() {
        std::fill(histL_.begin(), histL_.end(), 0.f);
        std::fill(histR_.begin(), histR_.end(), 0.f);
        pendingIn_L_.clear();
        pendingIn_R_.clear();
        outQueue_L_.clear();
        outQueue_R_.clear();
        crossfadeActive_ = false;
    }

    // azimuthDeg: -90(izq)..+90(der). aggressiveness: [0,1], mapeado desde
    // el control de UI existente (ver integración en PDEngine).
    void set_position(float azimuthDeg, float aggressiveness) {
        azimuthDeg = std::clamp(azimuthDeg, -90.f, 90.f);
        aggressiveness = std::clamp(aggressiveness, 0.f, 1.f);

        // Cuantización a pasos de 5° — evita recalcular FFT de la IR en
        // cada bloque si el ángulo varía por ruido de control mínimo, y
        // limita cuántas transiciones de crossfade ocurren por segundo.
        const float quant = std::round(azimuthDeg / 5.f) * 5.f;
        if (std::fabs(quant - lastAzimuth_) < 0.01f &&
            std::fabs(aggressiveness - lastAggr_) < 0.01f) {
            return; // sin cambio real, no dispara recomputo/crossfade
        }
        lastAzimuth_ = quant;
        lastAggr_    = aggressiveness;

        // El filtro activo pasa a "anterior" (para crossfade) y se computa
        // el nuevo filtro en el slot activo.
        std::swap(activeIdx_, prevIdx_);
        compute_filter(quant, aggressiveness, activeIdx_);
        crossfadeActive_ = true;
        crossfadeSampleCounter_ = 0;
    }

    // Procesa n muestras estéreo in-place-friendly (buffers separados).
    void process(const float* inL, const float* inR,
                 float* outL, float* outR, int n) {
        // Acumula entrada pendiente
        for (int i = 0; i < n; ++i) {
            pendingIn_L_.push_back(inL[i]);
            pendingIn_R_.push_back(inR[i]);
        }

        // Procesa todos los sub-bloques completos disponibles
        while ((int)pendingIn_L_.size() >= BLOCK) {
            process_one_block();
            pendingIn_L_.erase(pendingIn_L_.begin(), pendingIn_L_.begin() + BLOCK);
            pendingIn_R_.erase(pendingIn_R_.begin(), pendingIn_R_.begin() + BLOCK);
        }

        // Entrega tantas muestras de salida como se pidieron (n) si ya
        // hay disponibles en la cola; si no hay suficientes (arranque),
        // rellena con silencio — se pondrá al día en próximos bloques.
        int avail = std::min((int)outQueue_L_.size(), n);
        for (int i = 0; i < avail; ++i) { outL[i] = outQueue_L_[i]; outR[i] = outQueue_R_[i]; }
        for (int i = avail; i < n; ++i) { outL[i] = 0.f; outR[i] = 0.f; }
        outQueue_L_.erase(outQueue_L_.begin(), outQueue_L_.begin() + avail);
        outQueue_R_.erase(outQueue_R_.begin(), outQueue_R_.begin() + avail);
    }

private:
    static int next_pow2(int v) noexcept {
        int p = 1; while (p < v) p <<= 1; return p;
    }

    struct FilterFD {
        std::vector<float> Hre_L, Him_L;
        std::vector<float> Hre_R, Him_R;
    };

    void compute_filter(float azimuthDeg, float aggressiveness, int slot) {
        HRIRPair ir = hrtf_.generate(azimuthDeg, aggressiveness);

        auto toFreqDomain = [&](const std::vector<float>& ir_time,
                                 std::vector<float>& outRe, std::vector<float>& outIm) {
            outRe.assign(fftSize_, 0.f);
            outIm.assign(fftSize_, 0.f);
            for (size_t i = 0; i < ir_time.size() && (int)i < fftSize_; ++i)
                outRe[i] = ir_time[i];
            fft_->forward(outRe.data(), outIm.data());
        };

        toFreqDomain(ir.L, filters_[slot].Hre_L, filters_[slot].Him_L);
        toFreqDomain(ir.R, filters_[slot].Hre_R, filters_[slot].Him_R);
    }

    // Convoluciona un sub-bloque completo (BLOCK muestras) usando overlap-save
    // con el filtro dado por slot, devuelve BLOCK muestras válidas en outBuf.
    void convolve_block(int slot, const std::vector<float>& histL,
                         const std::vector<float>& histR,
                         std::vector<float>& outBufL, std::vector<float>& outBufR) {
        std::vector<float> reL(histL.begin(), histL.end()), imL(fftSize_, 0.f);
        std::vector<float> reR(histR.begin(), histR.end()), imR(fftSize_, 0.f);

        fft_->forward(reL.data(), imL.data());
        fft_->forward(reR.data(), imR.data());

        // Renderizado binaural: cada oído de salida combina AMBOS canales
        // de entrada (mezcla a mono antes de aplicar cada HRIR) — el
        // objetivo es una imagen espacial única por la posición de la
        // fuente, no preservar el estéreo original independientemente.
        std::vector<float> monoRe(fftSize_), monoIm(fftSize_);
        for (int i = 0; i < fftSize_; ++i) {
            monoRe[i] = 0.5f * (reL[i] + reR[i]);
            monoIm[i] = 0.5f * (imL[i] + imR[i]);
        }

        const FilterFD& F = filters_[slot];
        std::vector<float> yReL(fftSize_), yImL(fftSize_);
        std::vector<float> yReR(fftSize_), yImR(fftSize_);
        for (int i = 0; i < fftSize_; ++i) {
            // Multiplicación compleja: Y = X * H
            yReL[i] = monoRe[i] * F.Hre_L[i] - monoIm[i] * F.Him_L[i];
            yImL[i] = monoRe[i] * F.Him_L[i] + monoIm[i] * F.Hre_L[i];
            yReR[i] = monoRe[i] * F.Hre_R[i] - monoIm[i] * F.Him_R[i];
            yImR[i] = monoRe[i] * F.Him_R[i] + monoIm[i] * F.Hre_R[i];
        }
        fft_->inverse(yReL.data(), yImL.data());
        fft_->inverse(yReR.data(), yImR.data());

        // Overlap-save: descarta las primeras (IR_LEN-1) muestras (alias
        // circular), toma las BLOCK finales.
        outBufL.assign(BLOCK, 0.f);
        outBufR.assign(BLOCK, 0.f);
        const int validStart = fftSize_ - BLOCK;
        for (int i = 0; i < BLOCK; ++i) {
            outBufL[i] = yReL[validStart + i];
            outBufR[i] = yReR[validStart + i];
        }
    }

    void process_one_block() {
        // Desplaza historial (fftSize_ muestras) e inserta el nuevo bloque
        // al final — ventana deslizante para overlap-save.
        std::rotate(histL_.begin(), histL_.begin() + BLOCK, histL_.end());
        std::rotate(histR_.begin(), histR_.begin() + BLOCK, histR_.end());
        for (int i = 0; i < BLOCK; ++i) {
            histL_[fftSize_ - BLOCK + i] = pendingIn_L_[i];
            histR_[fftSize_ - BLOCK + i] = pendingIn_R_[i];
        }

        std::vector<float> blockL, blockR;

        if (!crossfadeActive_) {
            convolve_block(activeIdx_, histL_, histR_, blockL, blockR);
        } else {
            std::vector<float> newL, newR, oldL, oldR;
            convolve_block(activeIdx_, histL_, histR_, newL, newR);
            convolve_block(prevIdx_,   histL_, histR_, oldL, oldR);

            blockL.assign(BLOCK, 0.f);
            blockR.assign(BLOCK, 0.f);
            // Crossfade lineal a lo largo de este bloque (256 muestras
            // ≈ 5.3ms @ 48kHz) — suficientemente rápido para sentirse
            // responsivo, suficientemente lento para no producir zipper.
            for (int i = 0; i < BLOCK; ++i) {
                const float t = (float)i / (float)(BLOCK - 1);
                blockL[i] = oldL[i] * (1.f - t) + newL[i] * t;
                blockR[i] = oldR[i] * (1.f - t) + newR[i] * t;
            }
            crossfadeActive_ = false; // un solo bloque de crossfade por cambio
        }

        for (int i = 0; i < BLOCK; ++i) {
            outQueue_L_.push_back(blockL[i]);
            outQueue_R_.push_back(blockR[i]);
        }
    }

    float    sr_ = 48000.f;
    int      fftSize_ = 512;
    std::unique_ptr<FFTRadix2> fft_;

    SyntheticHRTF hrtf_;
    FilterFD filters_[2];   // doble buffer: activo + anterior (para crossfade)
    int      activeIdx_ = 0;
    int      prevIdx_   = 1;
    bool     crossfadeActive_ = false;
    int      crossfadeSampleCounter_ = 0;

    float    lastAzimuth_ = 1e9f;  // fuerza recompute en la primera llamada
    float    lastAggr_    = -1.f;

    std::vector<float> histL_, histR_;               // ventana deslizante (fftSize_)
    std::vector<float> pendingIn_L_, pendingIn_R_;    // entrada acumulada < BLOCK
    std::vector<float> outQueue_L_, outQueue_R_;      // salida lista para entregar
};

} // namespace ivanna
