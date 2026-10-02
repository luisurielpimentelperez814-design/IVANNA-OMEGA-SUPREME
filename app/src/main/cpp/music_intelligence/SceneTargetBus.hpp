// SceneTargetBus.hpp — Triple Buffer Wait-Free + Guarda Cibernética de Realismo M10
// + Controles Atómicos de Singularidad Acústica Atlas-Escena.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Arquitectura Wait-Free (Sección 4.5a / M10 / R1 / R9):
//   - Triple buffer lock-free y wait-free O(1) con std::atomic<SceneApply*>::exchange
//     (mismo patrón certificado en IvannaAudioClassifier::m_outputPool).
//   - Cero mutex, cero spinlocks, cero torn reads bajo concurrencia extrema (TSan clean).
//   - Incluye el evaluador en tiempo real de las métricas M10 (C_t, C_s, C_d, Q)
//     y la guarda automática de transitorios y mono-compatibilidad (reducción x0.70
//     con rampa suave de 100 ms si C_t < 0.85 o rho_out < -0.20).
#pragma once

#include "StyleBlender.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace ivanna { namespace ime {

struct alignas(64) SceneApply {
    SceneTargets t{};
    float    gate          = 0.0f; // smoothstep(0.35, 0.60, conf)
    float    conf          = 0.0f; // 1 - H(p)/ln(K)
    int32_t  styleIndex    = -1;   // 0..11
    float    presenceRatio = 0.12f;// f6 (3-8 kHz) para M4 excWet
    float    flatness1m    = 0.30f;// f8 (1 - SFM) para Anti-IMD en ChebShaper
    float    lateRatio     = 0.0f; // rhoLate medido por LateReverbSuppressor (M5)
    uint32_t seq           = 0;    // Monotónico (> 0 indica escena publicada)
};

// Métricas de realismo acústico M10 (telemetría, recompensa de entrenamiento y guarda en vivo)
struct RealismMetricsM10 {
    float cTransient  = 1.0f; // C_t: preservación de envolvente/transitorios [0..1]
    float cSpatial    = 1.0f; // C_s: 1 - |IACC_out - IACC_target(style)| [0..1]
    float cDynamic    = 1.0f; // C_d: 1 - clamp(|crest_out - crest_in| / 12 dB, 0, 1) [0..1]
    float qScore      = 1.0f; // Q = max(0.05, C_t^0.4 * C_s^0.3 * C_d^0.3) [0.05..1]
    float iaccOut     = 0.5f; // Correlación interaural de salida [-1..1]
    float crestInDb   = 12.0f;
    float crestOutDb  = 12.0f;
    float guardScale  = 1.0f; // 1.0 = nominal, 0.70 = guarda M10 activa
    bool  guardActive = false;
};

class alignas(64) SceneTargetBus {
public:
    SceneTargetBus() noexcept {
        pool_[0] = SceneApply{};
        pool_[1] = SceneApply{};
        pool_[2] = SceneApply{};
        state_.store(0u, std::memory_order_relaxed);
        readIdx_  = 1u;
        writeIdx_ = 2u;
    }

    // Singleton por proceso (libivanna_omega.so en app / libomega_effect.so en audioserver)
    static SceneTargetBus& instance() noexcept {
        static SceneTargetBus s_bus;
        return s_bus;
    }

    // Publicador (hilo worker / amortizado): wait-free O(1) con palabra atómica única (cleanIdx | dirtyBit)
    void publish(const SceneApply& in) noexcept {
        const uint32_t nextSeq = pubSeq_.fetch_add(1u, std::memory_order_relaxed) + 1u;
        pool_[writeIdx_] = in;
        pool_[writeIdx_].seq = nextSeq;
        const uint8_t nextState = static_cast<uint8_t>((writeIdx_ & 0x3u) | 0x4u);
        const uint8_t prevState = state_.exchange(nextState, std::memory_order_acq_rel);
        writeIdx_ = prevState & 0x3u;
    }

    // Consumidor (hilo de audio RT): wait-free O(1), devuelve true si había un frame nuevo
    bool consume(SceneApply& out) noexcept {
        if (!sceneReconstructionEnabled_.load(std::memory_order_relaxed)) {
            out = SceneApply{};
            return false;
        }
        const uint8_t cur = state_.load(std::memory_order_acquire);
        if ((cur & 0x4u) != 0u) {
            const uint8_t nextState = static_cast<uint8_t>(readIdx_ & 0x3u);
            const uint8_t prevState = state_.exchange(nextState, std::memory_order_acq_rel);
            readIdx_ = prevState & 0x3u;
            out = pool_[readIdx_];
            return true;
        }
        out = pool_[readIdx_];
        return false;
    }

    // Lectura no destructiva para telemetría / UI o múltiples etapas en el mismo bloque
    SceneApply peekLatest() const noexcept {
        if (!sceneReconstructionEnabled_.load(std::memory_order_relaxed)) {
            return SceneApply{};
        }
        const uint8_t cur = state_.load(std::memory_order_acquire);
        const uint8_t idx = ((cur & 0x4u) != 0u) ? (cur & 0x3u) : (readIdx_ & 0x3u);
        return pool_[idx];
    }

    // ── Controles Atómicos Maestros (R9 y Sección 5) ─────────────────────────
    void setSceneReconstructionEnabled(bool on) noexcept {
        sceneReconstructionEnabled_.store(on, std::memory_order_release);
    }
    bool isSceneReconstructionEnabled() const noexcept {
        return sceneReconstructionEnabled_.load(std::memory_order_acquire);
    }

    void setUseStatDereverb(bool on) noexcept {
        useStatDereverb_.store(on, std::memory_order_relaxed);
    }
    bool useStatDereverb() const noexcept {
        return useStatDereverb_.load(std::memory_order_relaxed);
    }

    void setUsePhysicalEr(bool on) noexcept {
        usePhysicalEr_.store(on, std::memory_order_relaxed);
    }
    bool usePhysicalEr() const noexcept {
        return usePhysicalEr_.load(std::memory_order_relaxed);
    }

    void setShaperMode(int mode) noexcept {
        shaperMode_.store(std::clamp(mode, 0, 1), std::memory_order_relaxed);
    }
    int shaperMode() const noexcept {
        return shaperMode_.load(std::memory_order_relaxed);
    }

    void setManualStyleOverride(int styleIdx) noexcept {
        manualStyleOverride_.store(std::clamp(styleIdx, -1, kDefaultAtlasStyles - 1), std::memory_order_relaxed);
    }
    int manualStyleOverride() const noexcept {
        return manualStyleOverride_.load(std::memory_order_relaxed);
    }

    void setUserWarmthOverride(float warmthOrNeg) noexcept {
        if (!std::isfinite(warmthOrNeg) || warmthOrNeg < 0.0f) {
            userWarmthOverride_.store(-1.0f, std::memory_order_relaxed);
        } else {
            userWarmthOverride_.store(std::clamp(warmthOrNeg, 0.0f, 1.0f), std::memory_order_relaxed);
        }
    }
    float userWarmthOverride() const noexcept {
        return userWarmthOverride_.load(std::memory_order_relaxed);
    }

    void setMaxCeilings(float maxInvGain, float maxProjWet, float maxExcWet) noexcept {
        if (std::isfinite(maxInvGain))
            maxInvGainCeiling_.store(std::clamp(maxInvGain, 0.05f, 0.50f), std::memory_order_relaxed);
        if (std::isfinite(maxProjWet))
            maxProjWetCeiling_.store(std::clamp(maxProjWet, 0.05f, 0.50f), std::memory_order_relaxed);
        if (std::isfinite(maxExcWet))
            maxExcWetCeiling_.store(std::clamp(maxExcWet, 0.02f, 0.30f), std::memory_order_relaxed);
    }
    float maxInvGainCeiling() const noexcept { return maxInvGainCeiling_.load(std::memory_order_relaxed); }
    float maxProjWetCeiling() const noexcept { return maxProjWetCeiling_.load(std::memory_order_relaxed); }
    float maxExcWetCeiling()  const noexcept { return maxExcWetCeiling_.load(std::memory_order_relaxed); }

    void publishLateRatio(float rhoLate) noexcept {
        if (std::isfinite(rhoLate)) {
            measuredLateRatio_.store(std::clamp(rhoLate, 0.0f, 1.0f), std::memory_order_relaxed);
        }
    }
    float measuredLateRatio() const noexcept {
        return measuredLateRatio_.load(std::memory_order_relaxed);
    }

    // ── Evaluación y Guarda en Vivo M10 (RT-Safe, 0 allocs) ──────────────────
    // Calcula C_t, C_s, C_d, Q sobre el bloque procesado respecto a la entrada seca,
    // y actualiza la rampa de protección de 100 ms (x0.70 si C_t < 0.85 o rho_out < -0.20).
    float updateRealismM10(const float* __restrict inL,
                           const float* __restrict inR,
                           const float* __restrict outL,
                           const float* __restrict outR,
                           size_t n,
                           float sampleRate,
                           float targetIacc = 0.45f) noexcept {
        if (!inL || !inR || !outL || !outR || n == 0) {
            return guardScale_.load(std::memory_order_relaxed);
        }

        float sumInSq = 0.0f, sumOutSq = 0.0f, pkIn = 0.0f, pkOut = 0.0f;
        float dotEnv = 0.0f, normEnvIn = 0.0f, normEnvOut = 0.0f;
        float sLL = 0.0f, sRR = 0.0f, sLR = 0.0f;

        for (size_t i = 0; i < n; ++i) {
            const float il = std::isfinite(inL[i])  ? inL[i]  : 0.0f;
            const float ir = std::isfinite(inR[i])  ? inR[i]  : 0.0f;
            const float ol = std::isfinite(outL[i]) ? outL[i] : 0.0f;
            const float orr = std::isfinite(outR[i]) ? outR[i] : 0.0f;

            const float eIn  = 0.5f * (std::fabs(il) + std::fabs(ir));
            const float eOut = 0.5f * (std::fabs(ol) + std::fabs(orr));
            pkIn  = std::max(pkIn,  std::max(std::fabs(il), std::fabs(ir)));
            pkOut = std::max(pkOut, std::max(std::fabs(ol), std::fabs(orr)));

            sumInSq  += 0.5f * (il * il + ir * ir);
            sumOutSq += 0.5f * (ol * ol + orr * orr);

            dotEnv     += eIn * eOut;
            normEnvIn  += eIn * eIn;
            normEnvOut += eOut * eOut;

            sLL += ol * ol;
            sRR += orr * orr;
            sLR += ol * orr;
        }

        const float invN   = 1.0f / static_cast<float>(n);
        const float rmsIn  = std::sqrt(sumInSq * invN);
        const float rmsOut = std::sqrt(sumOutSq * invN);

        // En silencio (< -60 dBFS), mantener métricas en 1.0 y guarda desactivada
        if (rmsIn < 1.0e-3f || rmsOut < 1.0e-3f) {
            const float sr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
            const float a100ms = 1.0f - std::exp(-static_cast<float>(n) / (0.100f * sr));
            float gs = guardScale_.load(std::memory_order_relaxed);
            gs += a100ms * (1.0f - gs);
            guardScale_.store(std::clamp(gs, 0.70f, 1.0f), std::memory_order_relaxed);
            return gs;
        }

        const float cT = std::clamp(dotEnv / (std::sqrt(normEnvIn * normEnvOut) + 1e-12f), 0.0f, 1.0f);
        const float rhoOut = std::clamp(sLR / (std::sqrt(sLL * sRR) + 1e-12f), -1.0f, 1.0f);
        const float cS = std::clamp(1.0f - std::fabs(rhoOut - std::clamp(targetIacc, -0.2f, 0.95f)), 0.0f, 1.0f);

        const float crestInDb  = 20.0f * std::log10((pkIn  + 1e-9f) / (rmsIn  + 1e-9f));
        const float crestOutDb = 20.0f * std::log10((pkOut + 1e-9f) / (rmsOut + 1e-9f));
        const float cD = 1.0f - std::clamp(std::fabs(crestOutDb - crestInDb) / 12.0f, 0.0f, 1.0f);

        const float qRaw = std::pow(std::max(cT, 1e-4f), 0.4f)
                         * std::pow(std::max(cS, 1e-4f), 0.3f)
                         * std::pow(std::max(cD, 1e-4f), 0.3f);
        const float qScore = std::clamp(qRaw, 0.05f, 1.0f);

        const bool triggerGuard = (cT < 0.85f) || (rhoOut < -0.20f);
        const float targetGuard = triggerGuard ? 0.70f : 1.0f;
        const float sr = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        const float a100ms = 1.0f - std::exp(-static_cast<float>(n) / (0.100f * sr));
        float gs = guardScale_.load(std::memory_order_relaxed);
        gs += a100ms * (targetGuard - gs);
        gs = std::clamp(gs, 0.70f, 1.0f);
        guardScale_.store(gs, std::memory_order_relaxed);

        // Suavizado ligero de métricas de telemetría para lectura estable
        cTransient_.store(0.85f * cTransient_.load(std::memory_order_relaxed) + 0.15f * cT, std::memory_order_relaxed);
        cSpatial_.store(0.85f * cSpatial_.load(std::memory_order_relaxed)     + 0.15f * cS, std::memory_order_relaxed);
        cDynamic_.store(0.85f * cDynamic_.load(std::memory_order_relaxed)     + 0.15f * cD, std::memory_order_relaxed);
        qScore_.store(0.85f * qScore_.load(std::memory_order_relaxed)         + 0.15f * qScore, std::memory_order_relaxed);
        iaccOut_.store(0.85f * iaccOut_.load(std::memory_order_relaxed)       + 0.15f * rhoOut, std::memory_order_relaxed);
        crestInDb_.store(crestInDb, std::memory_order_relaxed);
        crestOutDb_.store(crestOutDb, std::memory_order_relaxed);
        guardActive_.store(triggerGuard, std::memory_order_relaxed);

        return gs;
    }

    RealismMetricsM10 readRealismMetrics() const noexcept {
        RealismMetricsM10 m{};
        m.cTransient  = cTransient_.load(std::memory_order_relaxed);
        m.cSpatial    = cSpatial_.load(std::memory_order_relaxed);
        m.cDynamic    = cDynamic_.load(std::memory_order_relaxed);
        m.qScore      = qScore_.load(std::memory_order_relaxed);
        m.iaccOut     = iaccOut_.load(std::memory_order_relaxed);
        m.crestInDb   = crestInDb_.load(std::memory_order_relaxed);
        m.crestOutDb  = crestOutDb_.load(std::memory_order_relaxed);
        m.guardScale  = guardScale_.load(std::memory_order_relaxed);
        m.guardActive = guardActive_.load(std::memory_order_relaxed);
        return m;
    }

    float guardScale() const noexcept {
        return guardScale_.load(std::memory_order_relaxed);
    }

private:
    alignas(64) mutable SceneApply pool_[3]{};
    alignas(64) mutable std::atomic<uint8_t> state_{0u};
    alignas(64) uint8_t readIdx_{1u};
    alignas(64) uint8_t writeIdx_{2u};
    std::atomic<uint32_t> pubSeq_{0};

    // Controles atómicos globales del flanco Atlas-Escena (R9: default ON)
    std::atomic<bool>  sceneReconstructionEnabled_{true};
    std::atomic<bool>  useStatDereverb_{true};
    std::atomic<bool>  usePhysicalEr_{true};
    std::atomic<int>   shaperMode_{1};
    std::atomic<int>   manualStyleOverride_{-1};
    std::atomic<float> userWarmthOverride_{-1.0f};
    std::atomic<float> maxInvGainCeiling_{0.25f};
    std::atomic<float> maxProjWetCeiling_{0.25f};
    std::atomic<float> maxExcWetCeiling_{0.15f};
    std::atomic<float> measuredLateRatio_{0.0f};

    // Telemetría M10
    std::atomic<float> cTransient_{1.0f};
    std::atomic<float> cSpatial_{1.0f};
    std::atomic<float> cDynamic_{1.0f};
    std::atomic<float> qScore_{1.0f};
    std::atomic<float> iaccOut_{0.45f};
    std::atomic<float> crestInDb_{12.0f};
    std::atomic<float> crestOutDb_{12.0f};
    std::atomic<float> guardScale_{1.0f};
    std::atomic<bool>  guardActive_{false};
};

}} // namespace ivanna::ime
