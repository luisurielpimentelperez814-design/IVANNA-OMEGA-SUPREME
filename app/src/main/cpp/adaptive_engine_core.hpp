// adaptive_engine_core.hpp
// ============================================================================
// IVANNA ADAPTIVE ENGINE — Análisis inteligente + Control automático de parámetros
// Convierte CUALQUIER melodía en deleite auditivo
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS
// ============================================================================

#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include <atomic>

namespace ivanna::adaptive {

struct AudioCharacteristics {
    float rms = 0.0f;                    // Volumen
    float peak = 0.0f;                   // Pico máximo
    float spectralCentroid = 0.0f;       // Dónde está la energía (Hz)
    float spectralSpread = 0.0f;         // Amplitud del espectro
    float percussiveness = 0.0f;         // Cuánto "ataque" tiene (0-1)
    float tonality = 0.0f;               // Cuánto es tonal vs ruido (0-1)
    float reverbAmount = 0.0f;           // Cuánta reverberación detectada (0-1)
    float dynamicRange = 0.0f;           // Rango dinámico (0-1)
};

struct AdaptiveParameters {
    float compressorThreshold = -20.0f;  // dB
    float compressorRatio = 2.0f;
    float compressorAttack = 10.0f;      // ms
    float compressorRelease = 100.0f;    // ms
    
    float exciterAmount = 0.5f;          // 0-1
    float exciterFreq = 4000.0f;         // Hz
    
    float stereoWidth = 1.0f;            // 0-2 (1 = normal)
    float spatialIntensity = 0.7f;       // Cuánto spatial audio
    
    float eqBass = 0.0f;                 // dB boost/cut
    float eqMid = 0.0f;
    float eqTreble = 0.0f;
    
    float safetyMargin = 1.0f;           // Para evitar clipping
    float overallGain = 1.0f;            // Master gain adaptativo
};

class AdaptiveEngineCore {
private:
    static constexpr int FFT_SIZE = 2048;
    static constexpr int HISTORY_SIZE = 60;  // 60 frames = ~1.3 segundos @ 44.1kHz
    
    std::array<float, HISTORY_SIZE> rmsHistory{};
    std::array<float, HISTORY_SIZE> spectralHistory{};
    int historyIndex = 0;
    
    AudioCharacteristics currentChar{};
    AdaptiveParameters targetParams{};
    AdaptiveParameters smoothParams{};
    
    float smoothingFactor = 0.1f;  // Cambios suave (no bruscos)
    
public:
    AdaptiveEngineCore() = default;
    
    // Analizar características del audio
    void analyzeAudio(const float* audioBuffer, int bufferSize) {
        if (!audioBuffer || bufferSize <= 0) return;
        
        // 1. Calcular RMS (volumen)
        float sum = 0.0f;
        float maxPeak = 0.0f;
        for (int i = 0; i < bufferSize; ++i) {
            float sample = std::abs(audioBuffer[i]);
            sum += sample * sample;
            maxPeak = std::max(maxPeak, sample);
        }
        currentChar.rms = std::sqrt(sum / bufferSize);
        currentChar.peak = maxPeak;
        
        // 2. Detectar percussiveness (transientes/ataques)
        currentChar.percussiveness = detectPercussiveness(audioBuffer, bufferSize);
        
        // 3. Detectar tonality (música vs ruido)
        currentChar.tonality = detectTonality(audioBuffer, bufferSize);
        
        // 4. Detectar reverberación (por correlación de delays)
        currentChar.reverbAmount = detectReverb(audioBuffer, bufferSize);
        
        // 5. Rango dinámico
        currentChar.dynamicRange = calculateDynamicRange();
        
        // Guardar en historial
        rmsHistory[historyIndex] = currentChar.rms;
        spectralHistory[historyIndex] = currentChar.percussiveness;
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    }
    
    // Generar parámetros óptimos basado en el análisis
    void computeAdaptiveParameters() {
        // Reset a defaults
        targetParams = AdaptiveParameters{};
        
        // ━━━ COMPRESSOR ━━━━━━━━━━━━━━━━━━━━━━
        // Si hay mucho volumen, comprimir
        if (currentChar.rms > 0.5f) {
            targetParams.compressorThreshold = -15.0f - (currentChar.rms * 15.0f);
            targetParams.compressorRatio = 2.0f + (currentChar.dynamicRange * 2.0f);
        }
        
        // Si es percusivo, ataque rápido
        if (currentChar.percussiveness > 0.6f) {
            targetParams.compressorAttack = 5.0f;
            targetParams.compressorRelease = 50.0f;
        } else {
            targetParams.compressorAttack = 20.0f;
            targetParams.compressorRelease = 150.0f;
        }
        
        // ━━━ EXCITER (HarmonicExciter) ━━━━━━━
        // Si es muy plano, excitar más
        if (currentChar.tonality > 0.7f && currentChar.percussiveness < 0.4f) {
            targetParams.exciterAmount = 0.8f;  // Mucha excitación
            targetParams.exciterFreq = 5000.0f;  // Realce en altos
        } else if (currentChar.percussiveness > 0.7f) {
            targetParams.exciterAmount = 0.3f;  // Poco para mantener claridad
            targetParams.exciterFreq = 3000.0f;  // Midrange
        } else {
            targetParams.exciterAmount = 0.5f;  // Medio
            targetParams.exciterFreq = 4000.0f;
        }
        
        // ━━━ STEREO WIDTH ━━━━━━━━━━━━━━━━━━━
        // Si es monofónico o muy estrecho, expandir
        if (currentChar.tonality > 0.8f) {
            targetParams.stereoWidth = 1.5f;  // Estéreo mejorado
        } else {
            targetParams.stereoWidth = 1.2f;  // Expansión suave
        }
        targetParams.spatialIntensity = 0.8f;
        
        // ━━━ EQ ━━━━━━━━━━━━━━━━━━━━━━━━━━━
        // Detectar tipo de audio y ecualizar
        if (currentChar.spectralCentroid < 2000.0f) {
            // Mucho grave/bajo
            targetParams.eqBass = 3.0f;      // Boost bass
            targetParams.eqMid = 0.0f;
            targetParams.eqTreble = -2.0f;   // Cut treble para claridad
        } else if (currentChar.spectralCentroid > 6000.0f) {
            // Muy agudo/sibilante
            targetParams.eqBass = 0.0f;
            targetParams.eqMid = 2.0f;       // Boost mids para warmth
            targetParams.eqTreble = -3.0f;   // Cut harshness
        } else {
            // Balanceado - mantener natural
            targetParams.eqBass = 1.0f;
            targetParams.eqMid = 0.0f;
            targetParams.eqTreble = 1.0f;
        }
        
        // ━━━ SAFETY + GAIN ━━━━━━━━━━━━━━━━
        // Si hay reverb, reduce volumen para que no resuene
        if (currentChar.reverbAmount > 0.5f) {
            targetParams.safetyMargin = 0.8f;
        } else {
            targetParams.safetyMargin = 0.9f;
        }
        
        // Ganancia adaptativa: más ganancia en audio débil
        if (currentChar.rms < 0.3f) {
            targetParams.overallGain = 1.2f;
        } else if (currentChar.rms > 0.7f) {
            targetParams.overallGain = 0.85f;
        } else {
            targetParams.overallGain = 1.0f;
        }
    }
    
    // Obtener parámetros suavizados (evita cambios bruscos)
    const AdaptiveParameters& getSmoothParameters() {
        // Interpolación lineal suave
        smoothParams.compressorThreshold += 
            (targetParams.compressorThreshold - smoothParams.compressorThreshold) * smoothingFactor;
        smoothParams.compressorRatio += 
            (targetParams.compressorRatio - smoothParams.compressorRatio) * smoothingFactor;
        smoothParams.exciterAmount += 
            (targetParams.exciterAmount - smoothParams.exciterAmount) * smoothingFactor;
        smoothParams.stereoWidth += 
            (targetParams.stereoWidth - smoothParams.stereoWidth) * smoothingFactor;
        smoothParams.eqBass += 
            (targetParams.eqBass - smoothParams.eqBass) * smoothingFactor;
        smoothParams.eqMid += 
            (targetParams.eqMid - smoothParams.eqMid) * smoothingFactor;
        smoothParams.eqTreble += 
            (targetParams.eqTreble - smoothParams.eqTreble) * smoothingFactor;
        smoothParams.overallGain += 
            (targetParams.overallGain - smoothParams.overallGain) * smoothingFactor;
        
        return smoothParams;
    }
    
    const AudioCharacteristics& getCharacteristics() const {
        return currentChar;
    }

private:
    float detectPercussiveness(const float* buf, int len) {
        // Detectar transientes (cambios rápidos = percusivo)
        float maxDiff = 0.0f;
        for (int i = 1; i < std::min(len, 1024); ++i) {
            maxDiff = std::max(maxDiff, std::abs(buf[i] - buf[i-1]));
        }
        return std::min(1.0f, maxDiff * 10.0f);
    }
    
    float detectTonality(const float* buf, int len) {
        // Simplificado: tonality = cómo de periódico es el audio
        // Alto = tonal (música), Bajo = ruido
        float periodicity = 0.5f;  // Placeholder
        return std::min(1.0f, periodicity);
    }
    
    float detectReverb(const float* buf, int len) {
        // Detectar reverberación por cola de energía
        float tail = 0.0f;
        for (int i = std::max(0, len - 1024); i < len; ++i) {
            tail += std::abs(buf[i]);
        }
        tail /= std::min(1024, len);
        
        // Si hay energía en la cola, hay reverb
        return std::min(1.0f, tail * 5.0f);
    }
    
    float calculateDynamicRange() {
        // Calcular rango dinámico del historial
        float minRms = 1.0f, maxRms = 0.0f;
        for (float rms : rmsHistory) {
            if (rms > 0) {
                minRms = std::min(minRms, rms);
                maxRms = std::max(maxRms, rms);
            }
        }
        
        if (minRms == 1.0f || maxRms == 0.0f) return 0.0f;
        return (maxRms - minRms) / maxRms;
    }
};

}  // namespace ivanna::adaptive
