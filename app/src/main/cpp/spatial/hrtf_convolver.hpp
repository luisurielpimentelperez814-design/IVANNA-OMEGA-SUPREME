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

    // Activar optimizaciones de coma flotante por hardware en el hilo nativo
    ivanna::audio::enableAudioThreadFastMathOnce();

    // Calcular el tamaño óptimo de la FFT: potencia de 2 superior a (BLOCK + IR_LEN - 1) -> 256 + 128 - 1 = 383 -> 512
    fftSize_ = next_pow2(BLOCK + IR_LEN - 1);
    fft_ = std::make_unique<FFTRadix2>(fftSize_);

    // Inicializar buffers históricos de Overlap-Save
    histL_.assign(fftSize_, 0.0f);
    histR_.assign(fftSize_, 0.0f);
    
    // Reservas de memoria estáticas para evitar alojamientos dinámicos (malloc) en el hilo de audio
    pendingIn_L_.reserve(BLOCK * 4);
    pendingIn_R_.reserve(BLOCK * 4);
    outQueue_L_.reserve(BLOCK * 8);
    outQueue_R_.reserve(BLOCK * 8);

    // Buffers de trabajo para dominio de frecuencia (Pre-reserva completa)
    reL_.assign(fftSize_, 0.0f);   imL_.assign(fftSize_, 0.0f);
    reR_.assign(fftSize_, 0.0f);   imR_.assign(fftSize_, 0.0f);
    monoRe_.assign(fftSize_, 0.0f); monoIm_.assign(fftSize_, 0.0f);
    yReL_.assign(fftSize_, 0.0f);  yImL_.assign(fftSize_, 0.0f);
    yReR_.assign(fftSize_, 0.0f);  yImR_.assign(fftSize_, 0.0f);

    // Buffers para filtros de target y crossfade
    hrir_L_current_.assign(fftSize_, 0.0f); hrir_R_current_.assign(fftSize_, 0.0f);
    hrir_L_target_.assign(fftSize_, 0.0f);  hrir_R_target_.assign(fftSize_, 0.0f);
    
    H_ReL_curr_.assign(fftSize_, 0.0f);     H_ImL_curr_.assign(fftSize_, 0.0f);
    H_ReR_curr_.assign(fftSize_, 0.0f);     H_ImR_curr_.assign(fftSize_, 0.0f);
    H_ReL_targ_.assign(fftSize_, 0.0f);     H_ImL_targ_.assign(fftSize_, 0.0f);
    H_ReR_targ_.assign(fftSize_, 0.0f);     H_ImR_targ_.assign(fftSize_, 0.0f);

    // Inicializar coordenadas por defecto (Frente)
    currentAzimuth_ = 0.0f;
    currentElevation_ = 0.0f;
    targetAzimuth_ = 0.0f;
    targetElevation_ = 0.0f;
    
    // Cargar filtros iniciales en dominio temporal y transformarlos a frecuencia
    updateFilterResponses(0.0f, 0.0f, true);
    
    xfadeSamplesRemaining_ = 0;
    filterInitialized_ = true;
}

void HRTFConvolver::setTargetPosition(float azimuth, float elevation) noexcept {
    // Normalizar ángulos
    while (azimuth < -180.0f) azimuth += 360.0f;
    while (azimuth > 180.0f)  azimuth -= 360.0f;
    elevation = std::clamp(elevation, -45.0f, 90.0f);

    if (std::abs(azimuth - targetAzimuth_) > 0.1f || std::abs(elevation - targetElevation_) > 0.1f) {
        targetAzimuth_ = azimuth;
        targetElevation_ = elevation;
        
        // Si el filtro no se ha inicializado o está en reposo absoluto, forzar actualización inmediata
        if (xfadeSamplesRemaining_ == 0 && (currentAzimuth_ == targetAzimuth_) && (currentElevation_ == targetElevation_)) {
            updateFilterResponses(targetAzimuth_, targetElevation_, true);
        } else {
            // Disparar proceso de interpolación cruzada (Crossfade de bloque completo)
            updateFilterResponses(targetAzimuth_, targetElevation_, false);
            xfadeSamplesRemaining_ = XFADE_DURATION_SAMPLES;
        }
    }
}

void HRTFConvolver::process(const float* inputL, const float* inputR, float* outputL, float* outputR, uint32_t numSamples) noexcept {
    if (!filterInitialized_ || !inputL || !inputR || !outputL || !outputR || numSamples == 0) {
        if (outputL != inputL) std::memcpy(outputL, inputL, numSamples * sizeof(float));
        if (outputR != inputR) std::memcpy(outputR, inputR, numSamples * sizeof(float));
        return;
    }

    // 1. Acumular muestras de entrada en la cola interna de tamaño variable
    for (uint32_t i = 0; i < numSamples; ++i) {
        pendingIn_L_.push_back(inputL[i]);
        pendingIn_R_.push_back(inputR[i]);
    }

    // 2. Procesar en sub-bloques fijos de tamaño BLOCK (256 muestras) mediante Overlap-Save
    while (pendingIn_L_.size() >= static_cast<size_t>(BLOCK)) {
        
        // Desplazar el historial de entrada para Overlap-Save (Descartar las BLOCK más viejas)
        int overlapSize = fftSize_ - BLOCK; // 512 - 256 = 256
        std::memmove(histL_.data(), histL_.data() + BLOCK, overlapSize * sizeof(float));
        std::memmove(histR_.data(), histR_.data() + BLOCK, overlapSize * sizeof(float));

        // Copiar las nuevas BLOCK muestras al final de la línea de retraso del historial
        std::memcpy(histL_.data() + overlapSize, pendingIn_L_.data(), BLOCK * sizeof(float));
        std::memcpy(histR_.data() + overlapSize, pendingIn_R_.data(), BLOCK * sizeof(float));

        // Remover las muestras procesadas de la cola de entrada
        pendingIn_L_.erase(pendingIn_L_.begin(), pendingIn_L_.begin() + BLOCK);
        pendingIn_R_.erase(pendingIn_R_.begin(), pendingIn_R_.begin() + BLOCK);

        // --- CONVOLUCIÓN EN DOMINIO DE FRECUENCIA VIA FFT ---
        
        // Convertir la señal estéreo de entrada a una señal monoaural intermedia de referencia
        for (int i = 0; i < fftSize_; ++i) {
            monoRe_[i] = (histL_[i] + histR_[i]) * 0.5f;
            monoIm_[i] = 0.0f; // Audio real puro, imaginario en cero
            
            // Protección manual rápida contra denormales residuales que asfixian la FFT
            if (std::abs(monoRe_[i]) < 1e-30f) monoRe_[i] = 0.0f;
        }

        // Ejecutar FFT del bloque monoaural
        fft_->forward(monoRe_.data(), monoIm_.data());

        // Multiplicación compleja en dominio espectral con la HRIR Actual
        for (int i = 0; i < fftSize_; ++i) {
            yReL_[i] = monoRe_[i] * H_ReL_curr_[i] - monoIm_[i] * H_ImL_curr_[i];
            yImL_[i] = monoRe_[i] * H_ImL_curr_[i] + monoIm_[i] * H_ReL_curr_[i];

            yReR_[i] = monoRe_[i] * H_ReR_curr_[i] - monoIm_[i] * H_ImR_curr_[i];
            yImR_[i] = monoRe_[i] * H_ImR_curr_[i] + monoIm_[i] * H_ReR_curr_[i];
        }

        // Ejecutar IFFT para regresar al dominio del tiempo
        fft_->inverse(yReL_.data(), yImL_.data());
        fft_->inverse(yReR_.data(), yImR_.data());

        // --- LÓGICA DE CROSSFADE PARA CAMBIOS DE ÁNGULO EN TIEMPO REAL ---
        if (xfadeSamplesRemaining_ > 0) {
            // Convolución paralela secundaria con la HRIR Target
            for (int i = 0; i < fftSize_; ++i) {
                reL_[i] = monoRe_[i] * H_ReL_targ_[i] - monoIm_[i] * H_ImL_targ_[i];
                imL_[i] = monoRe_[i] * H_ImL_targ_[i] + monoIm_[i] * H_ReL_targ_[i];

                reR_[i] = monoRe_[i] * H_ReR_targ_[i] - monoIm_[i] * H_ImR_targ_[i];
                imR_[i] = monoRe_[i] * H_ImR_targ_[i] + monoIm_[i] * H_ReR_targ_[i];
            }

            fft_->inverse(reL_.data(), imL_.data());
            fft_->inverse(reR_.data(), imR_.data());

            // Mezcla lineal ponderada a lo largo del sub-bloque para el canal izquierdo y derecho
            for (int i = 0; i < BLOCK; ++i) {
                int outIdx = overlapSize + i; // Saltar la zona de aliasing circular (Overlap-Save)
                
                // Calcular factor de mezcla dinámico por muestra
                float progress = 1.0f - (static_cast<float>(xfadeSamplesRemaining_) / XFADE_DURATION_SAMPLES);
                progress = std::clamp(progress, 0.0f, 1.0f);

                float outSampleL = (1.0f - progress) * yReL_[outIdx] + progress * reL_[outIdx];
                float outSampleR = (1.0f - progress) * yReR_[outIdx] + progress * reR_[outIdx];

                outQueue_L_.push_back(outSampleL);
                outQueue_R_.push_back(outSampleR);

                if (xfadeSamplesRemaining_ > 0) {
                    --xfadeSamplesRemaining_;
                }
            }

            // Si el crossfade concluyó, consolidar el filtro target como el actual de forma definitiva
            if (xfadeSamplesRemaining_ == 0) {
                currentAzimuth_ = targetAzimuth_;
                currentElevation_ = targetElevation_;
                H_ReL_curr_ = H_ReL_targ_; H_ImL_curr_ = H_ImL_targ_;
                H_ReR_curr_ = H_ReR_targ_; H_ImR_curr_ = H_ImR_targ_;
            }
        } else {
            // Copiar directamente el resultado actual a la cola de salida omitiendo las primeras (IR_LEN - 1) muestras
            for (int i = 0; i < BLOCK; ++i) {
                int outIdx = overlapSize + i;
                outQueue_L_.push_back(yReL_[outIdx]);
                outQueue_R_.push_back(yReR_[outIdx]);
            }
        }
    }

    // 3. Extraer los datos procesados de la cola de salida y depositarlos en los buffers del DAC
    uint32_t samplesToDeliver = std::min(numSamples, static_cast<uint32_t>(outQueue_L_.size()));
    
    std::memcpy(outputL, outQueue_L_.data(), samplesToDeliver * sizeof(float));
    std::memcpy(outputR, outQueue_R_.data(), samplesToDeliver * sizeof(float));

    // Remover elementos entregados de la cola
    outQueue_L_.erase(outQueue_L_.begin(), outQueue_L_.begin() + samplesToDeliver);
    outQueue_R_.erase(outQueue_R_.begin(), outQueue_R_.begin() + samplesToDeliver);// Fallback de seguridad extrema: Si faltaron muestras por sub-bloques asíncronos, rellenar con silencio limpioif (samplesToDeliver < numSamples) {uint32_t remaining = numSamples - samplesToDeliver;std::memset(outputL + samplesToDeliver, 0, remaining * sizeof(float));std::memset(outputR + samplesToDeliver, 0, remaining * sizeof(float));}}void HRTFConvolver::updateFilterResponses(float azimuth, float elevation, bool immediate) noexcept {std::vector tempIR_L(IR_LEN, 0.0f);std::vector tempIR_R(IR_LEN, 0.0f);// Obtener los coeficientes sintéticos o muestreados del espacio acústicohrtf_.getHRIR(azimuth, elevation, tempIR_L.data(), tempIR_R.data());if (immediate) {std::fill(H_ReL_curr_.begin(), H_ImL_curr_.end(), 0.0f);std::fill(H_ImL_curr_.begin(), H_ImL_curr_.end(), 0.0f);std::fill(H_ReR_curr_.begin(), H_ReR_curr_.end(), 0.0f);std::fill(H_ImR_curr_.begin(), H_ImR_curr_.end(), 0.0f);std::memcpy(H_ReL_curr_.data(), tempIR_L.data(), IR_LEN * sizeof(float));std::memcpy(H_ReR_curr_.data(), tempIR_R.data(), IR_LEN * sizeof(float));// Transformar la respuesta al impulso temporal al dominio de frecuencia de inmediatofft_->forward(H_ReL_curr_.data(), H_ImL_curr_.data());fft_->forward(H_ReR_curr_.data(), H_ImR_curr_.data());currentAzimuth_ = azimuth;currentElevation_ = elevation;} else {std::fill(H_ReL_targ_.begin(), H_ImL_targ_.end(), 0.0f);std::fill(H_ImL_targ_.begin(), H_ImL_targ_.end(), 0.0f);std::fill(H_ReR_targ_.begin(), H_ReR_targ_.end(), 0.0f);std::fill(H_ImR_targ_.begin(), H_ImR_targ_.end(), 0.0f);std::memcpy(H_ReL_targ_.data(), tempIR_L.data(), IR_LEN * sizeof(float));std::memcpy(H_ReR_targ_.data(), tempIR_R.data(), IR_LEN * sizeof(float));// Cargar el espectro objetivo en los buffers de target para realizar el crossfade progresivofft_->forward(H_ReL_targ_.data(), H_ImL_targ_.data());fft_->forward(H_ReR_targ_.data(), H_ImR_targ_.data());}}uint32_t HRTFConvolver::next_pow2(uint32_t val) const noexcept {if (val == 0) return 1;val--;val |= val >> 1;  val |= val >> 2;val |= val >> 4;  val |= val >> 8;val |= val >> 16;return val + 1;}} // namespace ivanna
