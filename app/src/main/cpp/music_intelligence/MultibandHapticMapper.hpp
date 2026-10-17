// MultibandHapticMapper.hpp — Háptica multibanda 4D (sub, cuerpo, transitorio, textura)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// MOTIVO: BassHaptics/BassHapticAnalyzer (Kotlin) ya vibran con los golpes de graves,
// pero SOLO con graves y SIN anticipación. Aquí la háptica se reparte en cuatro bandas
// perceptivas independientes y se alimenta del ExcitationConductor, de modo que el
// teléfono empieza a vibrar ANTES del golpe auditivo (misma ventana de 5–10 ms), y
// añade dos capas que hoy no existen: el transitorio (ataque) y la textura (aire/densidad).
//
// SEGURIDAD: techo duro de amplitud (kHardCeiling) y atenuación automática por carga de
// dosis auditiva (setDoseLoad) — el mismo dosímetro que gobierna el oído gobierna la piel.
//
// RT-safe: sin malloc, sin locks, sin pow/exp en el camino caliente.
#pragma once

#include "MusicFeatureExtractor.hpp"
#include "ExcitationConductor.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace ivanna { namespace ime {

enum HapticBand : int {
    kHapticSub = 0,       // 20–80 Hz    → bombo, sub sostenido, retumbo (peso corporal)
    kHapticBody,          // 80–400 Hz   → cuerpo, toms, palm mute (pecho)
    kHapticTransient,     // ataque      → golpes secos, snare, clic (contacto)
    kHapticTexture,       // aire/densidad→ textura continua, ambiente (piel)
    kHapticBandCount
};

struct alignas(64) HapticFrame {
    float intensity[kHapticBandCount] = {0.f, 0.f, 0.f, 0.f}; // 0..1 por banda
    uint16_t amplitude[kHapticBandCount] = {0, 0, 0, 0};      // 0..255 (API Android de amplitud)
    uint16_t durationMs[kHapticBandCount] = {0, 0, 0, 0};     // duración sugerida del pulso
    float total = 0.0f;                                       // suma limitada (diagnóstico)
    float anticipation = 0.0f;                                // copia de la envolvente predictiva
    bool  active = false;
};

class alignas(64) MultibandHapticMapper {
public:
    static constexpr float kHardCeiling = 0.85f;   // techo absoluto de intensidad
    static constexpr float kDoseAttenuation = 0.55f; // atenuación máxima por dosis alta

    void prepare(float sampleRate, int maxBlock) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
        maxBlock_ = (maxBlock > 0) ? maxBlock : 256;
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < kHapticBandCount; ++i) {
            env_[i] = 0.0f;
            lastPulseMs_[i] = -1.0e9f;
        }
        tMs_ = 0.0f;
        prevOnsetEnergy_ = 0.0f;
        frame_ = HapticFrame{};
    }

    void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_relaxed); }
    bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    // Escala global de usuario [0, 0.85].
    void setScale(float s) noexcept {
        scale_.store(std::isfinite(s) ? std::clamp(s, 0.0f, kHardCeiling) : 0.0f, std::memory_order_relaxed);
    }
    float scale() const noexcept { return scale_.load(std::memory_order_relaxed); }

    // Carga de dosis auditiva [0,1] publicada por ExposureDosimeter. A dosis 1.0 la
    // háptica se atenúa a kDoseAttenuation para que la experiencia no empuje a subir
    // el volumen (correlación conocida entre háptica fuerte y exposición excesiva).
    void setDoseLoad(float d) noexcept {
        doseLoad_.store(std::isfinite(d) ? std::clamp(d, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    }
    float doseLoad() const noexcept { return doseLoad_.load(std::memory_order_relaxed); }

    // Retardo de la salida real (misma semántica que la sincronía háptica por ruta:
    // Bluetooth por códec, cable/USB 0). Permite alinear háptica y sonido percibido.
    void setOutputLatencyMs(float ms) noexcept {
        outLatency_.store(std::isfinite(ms) ? std::clamp(ms, 0.0f, 400.0f) : 0.0f, std::memory_order_relaxed);
    }
    float outputLatencyMs() const noexcept { return outLatency_.load(std::memory_order_relaxed); }

    // ── Avance por bloque ────────────────────────────────────────────────────
    // excitation: salida del ExcitationConductor (anticipa el golpe 5–10 ms).
    void update(const MusicFeatures& f, const ExcitationOutput& excitation, float blockMs) noexcept {
        const float dt = (std::isfinite(blockMs) && blockMs > 0.0f) ? blockMs
                       : (static_cast<float>(maxBlock_) / sr_) * 1000.0f;
        tMs_ += dt;
        if (!enabled_.load(std::memory_order_relaxed)) { frame_ = HapticFrame{}; return; }

        // ── Energías de banda del Atlas (partición complementaria del extractor) ──
        // subBandRatio (20–80), bodyBandRatio (80–400), defBandRatio (400–3k).
        const float subE  = std::clamp(f.subBandRatio, 0.0f, 1.0f);
        const float bodyE = std::clamp(f.bodyBandRatio * 1.4f, 0.0f, 1.0f);
        const float densE = std::clamp(f.density, 0.0f, 1.0f);
        const float airE  = std::clamp((f.airRatio + f.presenceRatio) * 1.6f, 0.0f, 1.0f);

        // ── Detección de transitorio (ataque) independiente del conductor ──
        const float onsetEnergy = std::clamp((f.crestDb / 24.0f) * (1.0f - f.flatness1m), 0.0f, 1.0f);
        const float onsetRise = std::max(0.0f, onsetEnergy - prevOnsetEnergy_);
        prevOnsetEnergy_ = onsetEnergy;

        // Anticipación: la háptica arranca antes que el campo audible (outLatency + lead).
        const float lead = excitation.leadMs + outLatency_.load(std::memory_order_relaxed);
        const float anticip = std::clamp(excitation.anticipation * (1.0f - std::clamp(lead / 400.0f, 0.0f, 0.35f)), 0.0f, 1.0f);

        // ── Objetivos por banda (ataque rápido / caída lenta e independiente) ──
        const float punch = excitation.predictedPunch;
        const float g = excitation.gate;
        float target[kHapticBandCount];
        target[kHapticSub]       = std::clamp(subE * (0.55f + 0.45f * punch) * (0.40f + 0.60f * anticip * g + 0.25f * anticip), 0.0f, 1.0f);
        target[kHapticBody]      = std::clamp(bodyE * (0.30f + 0.70f * anticip), 0.0f, 1.0f);
        target[kHapticTransient] = std::clamp(onsetRise * 3.5f + g * anticip * 0.45f, 0.0f, 1.0f);
        target[kHapticTexture]   = std::clamp(0.45f * densE + 0.55f * airE * (0.5f + 0.5f * anticip), 0.0f, 1.0f);

        // Constantes de tiempo por banda (ms): sub pesado, textura lenta, transitorio rápido.
        static constexpr float attackMs[kHapticBandCount]  = { 6.0f,  5.0f,  2.0f, 40.0f};
        static constexpr float releaseMs[kHapticBandCount] = {90.0f, 70.0f, 25.0f, 220.0f};
        static constexpr uint16_t durTable[kHapticBandCount] = {60, 45, 25, 120};
        static constexpr float minIntervalMs[kHapticBandCount] = {90.0f, 80.0f, 55.0f, 160.0f};

        const float dose = doseLoad_.load(std::memory_order_relaxed);
        const float doseGain = 1.0f - (1.0f - kDoseAttenuation) * dose;
        const float global = scale_.load(std::memory_order_relaxed) * doseGain;

        float total = 0.0f;
        for (int b = 0; b < kHapticBandCount; ++b) {
            const float tau = (target[b] > env_[b]) ? attackMs[b] : releaseMs[b];
            const float a = 1.0f - std::exp(-dt / std::max(tau, 0.5f));
            env_[b] += a * (target[b] - env_[b]);
            env_[b] = std::clamp(env_[b], 0.0f, 1.0f);

            // Refractario por banda: evita zumbido continuo en material muy denso.
            const bool canFire = (tMs_ - lastPulseMs_[b] >= minIntervalMs[b]);
            float v = env_[b] * global;
            if (!canFire) v *= 0.35f;          // sostenido, no re-disparado
            if (v > 0.02f && canFire && target[b] > 0.25f) lastPulseMs_[b] = tMs_;

            frame_.intensity[b]  = std::clamp(v, 0.0f, kHardCeiling);
            frame_.amplitude[b]  = (uint16_t)std::lround(frame_.intensity[b] * 255.0f);
            frame_.durationMs[b] = (uint16_t)(durTable[b] + (uint16_t)(lead > 1.0f ? lead : 0.0f));
            total += frame_.intensity[b];
        }

        frame_.total = std::clamp(total, 0.0f, kHardCeiling * (float)kHapticBandCount);
        frame_.anticipation = anticip;
        frame_.active = (frame_.intensity[kHapticSub] > 0.02f) || (frame_.intensity[kHapticBody] > 0.02f)
                     || (frame_.intensity[kHapticTransient] > 0.02f) || (frame_.intensity[kHapticTexture] > 0.02f);
    }

    HapticFrame frame() const noexcept { return frame_; }

    // Amplitud agregada (una sola celda háptica, para dispositivos sin control de amplitud).
    uint16_t aggregateAmplitude() const noexcept {
        float mx = 0.0f;
        for (int b = 0; b < kHapticBandCount; ++b) mx = std::max(mx, frame_.intensity[b]);
        return (uint16_t)std::lround(std::clamp(mx, 0.0f, kHardCeiling) * 255.0f);
    }

private:
    float sr_ = 48000.0f;
    int   maxBlock_ = 256;
    float tMs_ = 0.0f;
    float prevOnsetEnergy_ = 0.0f;
    float env_[kHapticBandCount] = {0.f, 0.f, 0.f, 0.f};
    float lastPulseMs_[kHapticBandCount] = {-1e9f, -1e9f, -1e9f, -1e9f};
    HapticFrame frame_{};

    std::atomic<bool>  enabled_{false};
    std::atomic<float> scale_{0.0f};
    std::atomic<float> doseLoad_{0.0f};
    std::atomic<float> outLatency_{0.0f};
};

}} // namespace ivanna::ime
