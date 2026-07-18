/*
 * ============================================================================
 * IVANNA-OMEGA-SUPREME — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 * Luis Uriel Pimentel Pérez (alias Gore TNS)
 *
 * Todos los modelos matemáticos, arquitecturas de sistema e implementaciones
 * de código contenidos en este archivo son propiedad intelectual exclusiva
 * del autor citado. Queda estrictamente prohibida la reproducción, distribución,
 * modificación o uso comercial no autorizado.
 *
 * Este software NO se distribuye bajo licencia CC0 ni dominio público.
 * Todos los derechos reservados. © 2026 Luis Uriel Pimentel Pérez.
 * ============================================================================
 */

#include "hrtf_convolver.hpp"
#include "fft_radix2.hpp"
#include "../include/audio_thread_priority.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace ivanna {

// FIX (ver comentario en hrtf_convolver.hpp): definiciones reales de
// constructor y destructor acá, donde fft_radix2.hpp (incluido arriba)
// da el tipo completo de FFTRadix2 que std::unique_ptr<FFTRadix2>
// necesita. '= default' alcanza para los dos — el compilador genera la
// construcción/destrucción correctas de fft_ automáticamente, solo
// necesitaban verse desde un lugar con el tipo completo visible.
HRTFConvolver::HRTFConvolver() = default;
HRTFConvolver::~HRTFConvolver() = default;

// -----------------------------------------------------------------------------
uint32_t HRTFConvolver::next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++v;
}

// -----------------------------------------------------------------------------
void HRTFConvolver::init(uint32_t sampleRate) {
    sr_ = sampleRate;
    hrtf_.init(sampleRate, IR_LEN);
    ivanna::audio::enableAudioThreadFastMathOnce();   // Si existe en tu código
    fftSize_ = next_pow2(BLOCK + IR_LEN - 1);
    fft_ = std::make_unique<FFTRadix2>(fftSize_);

    histL_.assign(fftSize_, 0.0f);
    histR_.assign(fftSize_, 0.0f);
    pendingIn_L_.assign(RING_BUFFER_SIZE, 0.0f);
    pendingIn_R_.assign(RING_BUFFER_SIZE, 0.0f);
    outQueue_L_.assign(RING_BUFFER_SIZE, 0.0f);
    outQueue_R_.assign(RING_BUFFER_SIZE, 0.0f);

    inReadPtr_ = 0; inWritePtr_ = 0; inCount_ = 0;
    outReadPtr_ = 0; outWritePtr_ = 0; outCount_ = 0;

    reL_.assign(fftSize_, 0.0f);   imL_.assign(fftSize_, 0.0f);
    reR_.assign(fftSize_, 0.0f);   imR_.assign(fftSize_, 0.0f);
    monoRe_.assign(fftSize_, 0.0f); monoIm_.assign(fftSize_, 0.0f);
    yReL_.assign(fftSize_, 0.0f);  yImL_.assign(fftSize_, 0.0f);
    yReR_.assign(fftSize_, 0.0f);  yImR_.assign(fftSize_, 0.0f);

    hrir_L_current_.assign(fftSize_, 0.0f); hrir_R_current_.assign(fftSize_, 0.0f);
    hrir_L_target_.assign(fftSize_, 0.0f);  hrir_R_target_.assign(fftSize_, 0.0f);
    H_ReL_curr_.assign(fftSize_, 0.0f);     H_ImL_curr_.assign(fftSize_, 0.0f);
    H_ReR_curr_.assign(fftSize_, 0.0f);     H_ImR_curr_.assign(fftSize_, 0.0f);
    H_ReL_targ_.assign(fftSize_, 0.0f);     H_ImL_targ_.assign(fftSize_, 0.0f);
    H_ReR_targ_.assign(fftSize_, 0.0f);     H_ImR_targ_.assign(fftSize_, 0.0f);

    currentAzimuth_       = 0.0f;
    currentAggressiveness_= 0.5f;
    targetAzimuth_.store(0.0f, std::memory_order_relaxed);
    targetAggressiveness_.store(0.5f, std::memory_order_relaxed);
    newTargetPending_.store(false, std::memory_order_relaxed);

    updateFilterResponses(0.0f, 0.5f, true);
    xfadeSamplesRemaining_.store(0, std::memory_order_relaxed);
    filterInitialized_ = true;
}

// -----------------------------------------------------------------------------
// Reinicia el estado dinámico (buffers, historial, crossfade) preservando la
// configuración ya calculada por init() (sr_, fftSize_, fft_). No reasigna
// FFTRadix2 ni reinicia SyntheticHRTF (no tienen estado dependiente de la
// posición). Usado por ObjectRenderer::reset() al soltar un virtual speaker.
void HRTFConvolver::reset() noexcept {
    if (!filterInitialized_) return;

    std::fill(histL_.begin(), histL_.end(), 0.0f);
    std::fill(histR_.begin(), histR_.end(), 0.0f);
    std::fill(pendingIn_L_.begin(), pendingIn_L_.end(), 0.0f);
    std::fill(pendingIn_R_.begin(), pendingIn_R_.end(), 0.0f);
    std::fill(outQueue_L_.begin(), outQueue_L_.end(), 0.0f);
    std::fill(outQueue_R_.begin(), outQueue_R_.end(), 0.0f);

    inReadPtr_ = 0; inWritePtr_ = 0; inCount_ = 0;
    outReadPtr_ = 0; outWritePtr_ = 0; outCount_ = 0;

    currentAzimuth_        = 0.0f;
    currentAggressiveness_ = 0.5f;
    targetAzimuth_.store(0.0f, std::memory_order_relaxed);
    targetAggressiveness_.store(0.5f, std::memory_order_relaxed);
    newTargetPending_.store(false, std::memory_order_relaxed);
    xfadeSamplesRemaining_.store(0, std::memory_order_relaxed);

    updateFilterResponses(0.0f, 0.5f, true);
}

// -----------------------------------------------------------------------------
void HRTFConvolver::set_position(float azimuthDeg, float aggressiveness) noexcept {
    while (azimuthDeg < -180.0f) azimuthDeg += 360.0f;
    while (azimuthDeg > 180.0f)  azimuthDeg -= 360.0f;
    aggressiveness = std::clamp(aggressiveness, 0.0f, 1.0f);

    float oldAz  = targetAzimuth_.load(std::memory_order_relaxed);
    float oldAgg = targetAggressiveness_.load(std::memory_order_relaxed);

    if (std::abs(azimuthDeg - oldAz) > 0.1f || std::abs(aggressiveness - oldAgg) > 0.01f) {
        targetAzimuth_.store(azimuthDeg, std::memory_order_release);
        targetAggressiveness_.store(aggressiveness, std::memory_order_release);
        newTargetPending_.store(true, std::memory_order_release);
    }
}

// -----------------------------------------------------------------------------
void HRTFConvolver::updateFilterResponses(float azimuthDeg, float aggressiveness, bool immediate) noexcept {
    const HRIRPair hrir = hrtf_.generate(azimuthDeg, aggressiveness);
    std::vector<float> irL = hrir.L;
    std::vector<float> irR = hrir.R;

    if (immediate) {
        std::fill(H_ReL_curr_.begin(), H_ReL_curr_.end(), 0.0f);
        std::fill(H_ImL_curr_.begin(), H_ImL_curr_.end(), 0.0f);
        std::fill(H_ReR_curr_.begin(), H_ReR_curr_.end(), 0.0f);
        std::fill(H_ImR_curr_.begin(), H_ImR_curr_.end(), 0.0f);
        std::memcpy(H_ReL_curr_.data(), irL.data(), IR_LEN * sizeof(float));
        std::memcpy(H_ReR_curr_.data(), irR.data(), IR_LEN * sizeof(float));
        fft_->forward(H_ReL_curr_.data(), H_ImL_curr_.data());
        fft_->forward(H_ReR_curr_.data(), H_ImR_curr_.data());

        currentAzimuth_        = azimuthDeg;
        currentAggressiveness_ = aggressiveness;
        xfadeSamplesRemaining_.store(0, std::memory_order_relaxed);
    } else {
        std::fill(H_ReL_targ_.begin(), H_ReL_targ_.end(), 0.0f);
        std::fill(H_ImL_targ_.begin(), H_ImL_targ_.end(), 0.0f);
        std::fill(H_ReR_targ_.begin(), H_ReR_targ_.end(), 0.0f);
        std::fill(H_ImR_targ_.begin(), H_ImR_targ_.end(), 0.0f);
        std::memcpy(H_ReL_targ_.data(), irL.data(), IR_LEN * sizeof(float));
        std::memcpy(H_ReR_targ_.data(), irR.data(), IR_LEN * sizeof(float));
        fft_->forward(H_ReL_targ_.data(), H_ImL_targ_.data());
        fft_->forward(H_ReR_targ_.data(), H_ImR_targ_.data());
    }
}

// -----------------------------------------------------------------------------
void HRTFConvolver::process(const float* inputL, const float* inputR,
                            float* outputL, float* outputR,
                            uint32_t numSamples) noexcept {
    if (!filterInitialized_ || !inputL || !inputR || !outputL || !outputR || numSamples == 0) {
        if (outputL != inputL) std::memcpy(outputL, inputL, numSamples * sizeof(float));
        if (outputR != inputR) std::memcpy(outputR, inputR, numSamples * sizeof(float));
        return;
    }

    // 1. Inserción al búfer circular de entrada
    for (uint32_t i = 0; i < numSamples; ++i) {
        if (inCount_ < RING_BUFFER_SIZE) {
            pendingIn_L_[inWritePtr_] = inputL[i];
            pendingIn_R_[inWritePtr_] = inputR[i];
            inWritePtr_ = (inWritePtr_ + 1) % RING_BUFFER_SIZE;
            ++inCount_;
        }
    }

    // 2. Procesar bloques completos (Overlap-Save)
    while (inCount_ >= static_cast<uint32_t>(BLOCK)) {
        // --- 2a. Verificar si hay que iniciar un crossfade ---
        bool pending = newTargetPending_.load(std::memory_order_acquire);
        int xfadeRemaining = xfadeSamplesRemaining_.load(std::memory_order_relaxed);
        if (pending && xfadeRemaining == 0) {
            float tAz = targetAzimuth_.load(std::memory_order_relaxed);
            float tAgg = targetAggressiveness_.load(std::memory_order_relaxed);
            newTargetPending_.store(false, std::memory_order_relaxed);

            if (std::abs(tAz - currentAzimuth_) > 0.1f ||
                std::abs(tAgg - currentAggressiveness_) > 0.01f) {
                updateFilterResponses(tAz, tAgg, false);
                xfadeSamplesRemaining_.store(XFADE_DURATION_SAMPLES, std::memory_order_relaxed);
            }
        }

        // 2b. Mover historial y leer nuevo bloque
        int overlapSize = fftSize_ - BLOCK;
        std::memmove(histL_.data(), histL_.data() + BLOCK, overlapSize * sizeof(float));
        std::memmove(histR_.data(), histR_.data() + BLOCK, overlapSize * sizeof(float));
        for (int i = 0; i < BLOCK; ++i) {
            histL_[overlapSize + i] = pendingIn_L_[inReadPtr_];
            histR_[overlapSize + i] = pendingIn_R_[inReadPtr_];
            inReadPtr_ = (inReadPtr_ + 1) % RING_BUFFER_SIZE;
        }
        inCount_ -= BLOCK;

        // 2c. Preparar mono y FFT
        for (int i = 0; i < fftSize_; ++i) {
            monoRe_[i] = (histL_[i] + histR_[i]) * 0.5f;
            monoIm_[i] = 0.0f;
            if (std::abs(monoRe_[i]) < 1e-30f) monoRe_[i] = 0.0f;
        }
        fft_->forward(monoRe_.data(), monoIm_.data());

        // 2d. Convolución con filtro actual (A)
        for (int i = 0; i < fftSize_; ++i) {
            yReL_[i] = monoRe_[i] * H_ReL_curr_[i] - monoIm_[i] * H_ImL_curr_[i];
            yImL_[i] = monoRe_[i] * H_ImL_curr_[i] + monoIm_[i] * H_ReL_curr_[i];
            yReR_[i] = monoRe_[i] * H_ReR_curr_[i] - monoIm_[i] * H_ImR_curr_[i];
            yImR_[i] = monoRe_[i] * H_ImR_curr_[i] + monoIm_[i] * H_ReR_curr_[i];
        }
        fft_->inverse(yReL_.data(), yImL_.data());
        fft_->inverse(yReR_.data(), yImR_.data());
        float scale = 1.0f / static_cast<float>(fftSize_);

        // 2e. Salida con o sin crossfade
        xfadeRemaining = xfadeSamplesRemaining_.load(std::memory_order_relaxed);
        if (xfadeRemaining > 0) {
            // Convolución con filtro destino (B)
            for (int i = 0; i < fftSize_; ++i) {
                reL_[i] = monoRe_[i] * H_ReL_targ_[i] - monoIm_[i] * H_ImL_targ_[i];
                imL_[i] = monoRe_[i] * H_ImL_targ_[i] + monoIm_[i] * H_ReL_targ_[i];
                reR_[i] = monoRe_[i] * H_ReR_targ_[i] - monoIm_[i] * H_ImR_targ_[i];
                imR_[i] = monoRe_[i] * H_ImR_targ_[i] + monoIm_[i] * H_ReR_targ_[i];
            }
            fft_->inverse(reL_.data(), imL_.data());
            fft_->inverse(reR_.data(), imR_.data());

            // Interpolación muestra a muestra (progress dentro del bucle)
            for (int i = 0; i < BLOCK; ++i) {
                xfadeRemaining = xfadeSamplesRemaining_.load(std::memory_order_relaxed);
                if (xfadeRemaining <= 0) break; // seguridad

                float progress = 1.0f - (static_cast<float>(xfadeRemaining) / XFADE_DURATION_SAMPLES);
                progress = std::clamp(progress, 0.0f, 1.0f);

                int outIdx = overlapSize + i;
                float currL = yReL_[outIdx] * scale;
                float currR = yReR_[outIdx] * scale;
                float targL = reL_[outIdx] * scale;
                float targR = reR_[outIdx] * scale;

                float finalL = (1.0f - progress) * currL + progress * targL;
                float finalR = (1.0f - progress) * currR + progress * targR;

                if (outCount_ < RING_BUFFER_SIZE) {
                    outQueue_L_[outWritePtr_] = finalL;
                    outQueue_R_[outWritePtr_] = finalR;
                    outWritePtr_ = (outWritePtr_ + 1) % RING_BUFFER_SIZE;
                    ++outCount_;
                }
                xfadeSamplesRemaining_.store(xfadeRemaining - 1, std::memory_order_relaxed);
            }

            // Si se consumió todo el crossfade, intercambiar filtros
            if (xfadeSamplesRemaining_.load(std::memory_order_relaxed) == 0) {
                std::swap(H_ReL_curr_, H_ReL_targ_);
                std::swap(H_ImL_curr_, H_ImL_targ_);
                std::swap(H_ReR_curr_, H_ReR_targ_);
                std::swap(H_ImR_curr_, H_ImR_targ_);
                currentAzimuth_  = targetAzimuth_.load(std::memory_order_relaxed);
                currentAggressiveness_ = targetAggressiveness_.load(std::memory_order_relaxed);
            }
            // Si el bucle se rompió por xfadeRemaining==0, las muestras restantes del bloque
            // se procesarán en el siguiente bucle (ya con el filtro actual).
        } else {
            // Sin crossfade: salida directa
            for (int i = 0; i < BLOCK; ++i) {
                int outIdx = overlapSize + i;
                if (outCount_ < RING_BUFFER_SIZE) {
                    outQueue_L_[outWritePtr_] = yReL_[outIdx] * scale;
                    outQueue_R_[outWritePtr_] = yReR_[outIdx] * scale;
                    outWritePtr_ = (outWritePtr_ + 1) % RING_BUFFER_SIZE;
                    ++outCount_;
                }
            }
        }
    }

    // 3. Entrega de muestras al buffer de salida del sistema
    uint32_t samplesToDeliver = std::min(numSamples, outCount_);
    for (uint32_t i = 0; i < samplesToDeliver; ++i) {
        outputL[i] = outQueue_L_[outReadPtr_];
        outputR[i] = outQueue_R_[outReadPtr_];
        outReadPtr_ = (outReadPtr_ + 1) % RING_BUFFER_SIZE;
        --outCount_;
    }

    if (samplesToDeliver < numSamples) {
        uint32_t missing = numSamples - samplesToDeliver;
        std::memset(outputL + samplesToDeliver, 0, missing * sizeof(float));
        std::memset(outputR + samplesToDeliver, 0, missing * sizeof(float));
    }
}

} // namespace ivanna
