/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
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

#include "HRTFConvolver.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace ivanna {

void HRTFConvolver::init(uint32_t sampleRate) {
    sr_ = sampleRate;
    hrtf_.init(sampleRate, IR_LEN);
    ivanna::audio::enableAudioThreadFastMathOnce();
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
    
    currentAzimuth_ = 0.0f;
    currentElevation_ = 0.0f;
    targetAzimuth_ = 0.0f;
    targetElevation_ = 0.0f;
    
    updateFilterResponses(0.0f, 0.0f, true);
    xfadeSamplesRemaining_ = 0;
    filterInitialized_ = true;
}

void HRTFConvolver::setTargetPosition(float azimuth, float elevation) noexcept {
    while (azimuth < -180.0f) azimuth += 360.0f;
    while (azimuth > 180.0f)  azimuth -= 360.0f;
    elevation = std::clamp(elevation, -45.0f, 90.0f);
    
    if (std::abs(azimuth - targetAzimuth_) > 0.1f || std::abs(elevation - targetElevation_) > 0.1f) {
        targetAzimuth_ = azimuth;
        targetElevation_ = elevation;
        if (xfadeSamplesRemaining_ == 0 && (currentAzimuth_ == targetAzimuth_) && (currentElevation_ == targetElevation_)) {
            updateFilterResponses(targetAzimuth_, targetElevation_, true);
        } else {
            updateFilterResponses(targetAzimuth_, targetElevation_, false);
            xfadeSamplesRemaining_ = XFADE_DURATION_SAMPLES;
        }
    }
}

void HRTFConvolver::updateFilterResponses(float azimuth, float elevation, bool immediate) noexcept {
    std::vector<float> irL(IR_LEN, 0.0f);
    std::vector<float> irR(IR_LEN, 0.0f);
    hrtf_.getIR(azimuth, elevation, irL.data(), irR.data());
    
    if (immediate) {
        std::fill(H_ReL_curr_.begin(), H_ReL_curr_.end(), 0.0f);
        std::fill(H_ImL_curr_.begin(), H_ImL_curr_.end(), 0.0f);
        std::fill(H_ReR_curr_.begin(), H_ReR_curr_.end(), 0.0f);
        std::fill(H_ImR_curr_.begin(), H_ImR_curr_.end(), 0.0f);
        std::memcpy(H_ReL_curr_.data(), irL.data(), IR_LEN * sizeof(float));
        std::memcpy(H_ReR_curr_.data(), irR.data(), IR_LEN * sizeof(float));
        fft_->forward(H_ReL_curr_.data(), H_ImL_curr_.data());
        fft_->forward(H_ReR_curr_.data(), H_ImR_curr_.data());
        currentAzimuth_ = azimuth;
        currentElevation_ = elevation;
        xfadeSamplesRemaining_ = 0;
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

void HRTFConvolver::process(const float* inputL, const float* inputR, float* outputL, float* outputR, uint32_t numSamples) noexcept {
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
            inCount_++;
        }
    }
    
    // 2. Procesamiento Overlap-Save espectral en bloques fijos de tamaño BLOCK
    while (inCount_ >= static_cast<uint32_t>(BLOCK)) {
        int overlapSize = fftSize_ - BLOCK;
        std::memmove(histL_.data(), histL_.data() + BLOCK, overlapSize * sizeof(float));
        std::memmove(histR_.data(), histR_.data() + BLOCK, overlapSize * sizeof(float));
        
        for (int i = 0; i < BLOCK; ++i) {
            histL_[overlapSize + i] = pendingIn_L_[inReadPtr_];
            histR_[overlapSize + i] = pendingIn_R_[inReadPtr_];
            inReadPtr_ = (inReadPtr_ + 1) % RING_BUFFER_SIZE;
        }
        inCount_ -= BLOCK;
        
        for (int i = 0; i < fftSize_; ++i) {
            monoRe_[i] = (histL_[i] + histR_[i]) * 0.5f;
            monoIm_[i] = 0.0f;
            if (std::abs(monoRe_[i]) < 1e-30f) monoRe_[i] = 0.0f;
        }
        
        fft_->forward(monoRe_.data(), monoIm_.data());
        
        // Convolución con el Filtro A (Current)
        for (int i = 0; i < fftSize_; ++i) {
            yReL_[i] = monoRe_[i] * H_ReL_curr_[i] - monoIm_[i] * H_ImL_curr_[i];
            yImL_[i] = monoRe_[i] * H_ImL_curr_[i] + monoIm_[i] * H_ReL_curr_[i];
            yReR_[i] = monoRe_[i] * H_ReR_curr_[i] - monoIm_[i] * H_ImR_curr_[i];
            yImR_[i] = monoRe_[i] * H_ImR_curr_[i] + monoIm_[i] * H_ReR_curr_[i];
        }
        fft_->inverse(yReL_.data(), yImL_.data());
        fft_->inverse(yReR_.data(), yImR_.data());
        
        float scale = 1.0f / static_cast<float>(fftSize_);
        
        if (xfadeSamplesRemaining_ > 0) {
            // Convolución con el Filtro B (Target)
            for (int i = 0; i < fftSize_; ++i) {
                reL_[i] = monoRe_[i] * H_ReL_targ_[i] - monoIm_[i] * H_ImL_targ_[i];
                imL_[i] = monoRe_[i] * H_ImL_targ_[i] + monoIm_[i] * H_ReL_targ_[i];
                reR_[i] = monoRe_[i] * H_ReR_targ_[i] - monoIm_[i] * H_ImR_targ_[i];
                imR_[i] = monoRe_[i] * H_ImR_targ_[i] + monoIm_[i] * H_ReR_targ_[i];
            }
            fft_->inverse(reL_.data(), imL_.data());
            fft_->inverse(reR_.data(), imR_.data());
            
            // Interpolación cruzada lineal libre de zipper-noise
            for (int i = 0; i < BLOCK; ++i) {
                int outIdx = overlapSize + i;
                float progress = 1.0f - (static_cast<float>(xfadeSamplesRemaining_) / XFADE_DURATION_SAMPLES);
                progress = std::clamp(progress, 0.0f, 1.0f);
                
                float currSampleL = yReL_[outIdx] * scale;
                float currSampleR = yReR_[outIdx] * scale;
                float targSampleL = reL_[outIdx] * scale;
                float targSampleR = reR_[outIdx] * scale;
                
                float finalL = (1.0f - progress) * currSampleL + progress * targSampleL;
                float finalR = (1.0f - progress) * currSampleR + progress * targSampleR;
                
                if (outCount_ < RING_BUFFER_SIZE) {
                    outQueue_L_[outWritePtr_] = finalL;
                    outQueue_R_[outWritePtr_] = finalR;
                    outWritePtr_ = (outWritePtr_ + 1) % RING_BUFFER_SIZE;
                    outCount_++;
                }
                if (xfadeSamplesRemaining_ > 0) xfadeSamplesRemaining_--;
            }
            
            if (xfadeSamplesRemaining_ == 0) {
                H_ReL_curr_ = H_ReL_targ_; H_ImL_curr_ = H_ImL_targ_;
                H_ReR_curr_ = H_ReR_targ_; H_ImR_curr_ = H_ImR_targ_;
                currentAzimuth_ = targetAzimuth_;
                currentElevation_ = targetElevation_;
            }
        } else {
            // Envío directo sin crossfade activo
            for (int i = 0; i < BLOCK; ++i) {
                int outIdx = overlapSize + i;
                if (outCount_ < RING_BUFFER_SIZE) {
                    outQueue_L_[outWritePtr_] = yReL_[outIdx] * scale;
                    outQueue_R_[outWritePtr_] = yReR_[outIdx] * scale;
                    outWritePtr_ = (outWritePtr_ + 1) % RING_BUFFER_SIZE;
                    outCount_++;
                }
            }
        }
    }
    
    // 3. Extracción y entrega de muestras al búfer de salida del sistema
        uint32_t samplesToDeliver = std::min(numSamples, outCount_);
    for (uint32_t i = 0; i < samplesToDeliver; ++i) {
        outputL[i] = outQueue_L_[outReadPtr_];
        outputR[i] = outQueue_R_[outReadPtr_];
        outReadPtr_ = (outReadPtr_ + 1) % RING_BUFFER_SIZE;
        outCount_--;
    }
    
    // Relleno preventivo con silencio si hay underrun parcial
    if (samplesToDeliver < numSamples) {
        uint32_t missing = numSamples - samplesToDeliver;
        std::memset(outputL + samplesToDeliver, 0, missing * sizeof(float));
        std::memset(outputR + samplesToDeliver, 0, missing * sizeof(float));
    }
}

} // namespace ivanna

