// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// OmniField.hpp — Operador Unificado de Campo Espacial (OMNI-FIELD), v1.
//
// Fusión matemática de 4 fuentes en UN SOLO operador (ver docs/OMNI_FIELD.md,
// ~/workspace/analysis-ivanna/OMNI_FIELD.md):
//   (a) Separación por fuentes estilo OmniSpatial (4 stems por crossover +
//       transitorios; el residual garantiza que la suma reproduce el mid).
//   (b) Personalización HRTF con gradiente escalado por MAA (30–52% menos
//       cómputo: diezmado por sector + umbral de rediseño = MAA del sector).
//   (c) Escena espacial auto-supervisada estilo AudioSphere (azimut dominante
//       + difusividad estimados de la propia señal, sin etiquetas).
//   (d) Campo neural INAF como renderer continuo por muestra (SIREN N1+N3,
//       FIR de fase mínima + retardo fraccional, sin STFT).
//
//   y[n] = (1−a)·x[n] + a · Σ_k R_{p'(θ_k, φ_k, r_k)} { s_k[n] }
//
// Cadena Ruta A: ORBE → HOLOS → OMNI-FIELD → Φ. ORBE y HOLOS SIGUEN ACTIVOS
// (directiva 2026-11-08); OMNI re-espacializa el campo ya mejorado por ellos.
// amount=0 ⇒ bypass bit-exacto. Fail-closed absoluto: entrada o parámetros no
// finitos ⇒ bypass; fallo de diseño FIR ⇒ se conserva el FIR anterior (o un
// FIR plano si no hay ninguno); renderer mudo con entrada presente ⇒ bypass.
// Cero allocations en process() (todo prealocado en prepare()).
//
// Contrato: header-only, C++17 portable, sin excepciones. prepare() DEBE
// llamarse antes de process() (lo hace nativeInit/nativeInitDSP); sin prepare,
// process() es bypass silencioso (fail-closed).
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "OmniFieldConvergence.hpp"
#include "OmniFieldRender.hpp"
#include "OmniFieldScene.hpp"
#include "OmniFieldSiren.hpp"

namespace ivanna {
namespace omnifield {

// Parámetros del operador (los lee el JNI cada bloque desde los atomics).
struct OmniParams {
    float amount = 1.0f;        // 0 = bypass bit-exacto, 1 = render completo
    float scenePull = 0.3f;     // atracción de stems hacia el centroide (0..1)
    float distanceM = 1.2f;     // distancia de la voz (m); escala r de todos
    bool headTracking = true;   // si false, el yaw se ignora
    bool convergence = true;    // convergencia Omega–SAFMUS (Kuramoto +
                                // fusión por r + tono emergente); false =
                                // comportamiento previo (K=0, sin tono
                                // emergente, pull_eff = pull)
};

class OmniField {
public:
    static constexpr int kNumStems = StemSeparator::kNumStems;  // 4
    static constexpr int kChunk = 512;  // frames por sub-bloque interno

    // Posiciones canónicas por stem: (azimut°, elevación°, distancia m).
    static constexpr float kStemAzDeg[kNumStems] = {0.0f, -30.0f, 0.0f, 60.0f};
    static constexpr float kStemElDeg[kNumStems] = {0.0f, 0.0f, -10.0f, 15.0f};
    static constexpr float kStemDistM[kNumStems] = {1.2f, 1.6f, 1.0f, 2.0f};

    OmniField() = default;
    OmniField(const OmniField&) = delete;
    OmniField& operator=(const OmniField&) = delete;

    // Prepara todo (puede allocar: FFT del diseñador). Idempotente.
    void prepare(float sampleRate) {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f &&
                       sampleRate <= 192000.0f)
                          ? sampleRate
                          : 48000.0f;
        separator_.prepare(sampleRate_);
        scene_.prepare(sampleRate_);
        personalizer_.prepare(sampleRate_);
        designer_.prepare(sampleRate_);
        kuramoto_.prepare(sampleRate_);
        for (int k = 0; k < kNumStems; ++k) renderers_[k].prepare(sampleRate_);
        emergentRenderer_.prepare(sampleRate_);
        prepared_ = true;
        reset();
    }

    void reset() noexcept {
        processCalls_.store(0, std::memory_order_relaxed);
        blocksRendered_.store(0, std::memory_order_relaxed);
        framesRendered_.store(0, std::memory_order_relaxed);
        separator_.reset();
        scene_.reset();
        personalizer_.reset();
        kuramoto_.reset();
        emergentRenderer_.reset();
        emergentFirValid_ = false;
        emergentLastTheta_ = 0.0f;
        emergentLastR_ = 0.0f;
        rBlock_ = 0.0f;
        energyGate_ = 0.0f;
        for (int k = 0; k < kNumStems; ++k) {
            renderers_[k].reset();
            firValid_[k] = false;
            lastTheta_[k] = 0.0f;
            lastR_[k] = 0.0f;
        }
        appliedAmount_ = std::isfinite(params_.amount)
                             ? std::clamp(params_.amount, 0.0f, 1.0f)
                             : 0.0f;
        yawRad_ = 0.0f;
        fault_ = false;
        designCount_ = 0;
        blockIndex_ = 0;
        levelGain_ = 1.0f;
        hasProcessed_ = false;
    }

    void setParams(const OmniParams& p) noexcept {
        params_.amount = std::isfinite(p.amount) ? std::clamp(p.amount, 0.0f, 1.0f) : 0.0f;
        params_.scenePull =
            std::isfinite(p.scenePull) ? std::clamp(p.scenePull, 0.0f, 1.0f) : 0.3f;
        params_.distanceM =
            std::isfinite(p.distanceM) ? std::clamp(p.distanceM, 0.5f, 2.0f) : 1.2f;
        params_.headTracking = p.headTracking;
        params_.convergence = p.convergence;
        // Antes del primer process() no ha fluido audio: el amount aplicado
        // sigue al objetivo sin rampa (así amount=0 es bypass bit-exacto
        // desde el primer bloque). Después, los cambios de amount usan la
        // rampa suave de processChunk para no hacer clic.
        if (!hasProcessed_) appliedAmount_ = params_.amount;
    }

    void setAmount(float a) noexcept {
        params_.amount = std::isfinite(a) ? std::clamp(a, 0.0f, 1.0f) : 0.0f;
        if (!hasProcessed_) appliedAmount_ = params_.amount;
        if (params_.amount == 0.0f) fault_ = false;  // re-arme (como INAF)
    }

    void setHeadPose(float yawRad) noexcept {
        yawRad_ = std::isfinite(yawRad)
                      ? std::clamp(yawRad, -3.14159265f, 3.14159265f)
                      : 0.0f;
    }

    void setAudiogram(const float lossL[6], const float lossR[6]) noexcept {
        personalizer_.setAudiogram(lossL, lossR);
    }

    // In-place, cualquier n>0. amount=0 ⇒ no toca los buffers.
    void process(float* l, float* r, int n) noexcept {
        if (!prepared_ || l == nullptr || r == nullptr || n <= 0) return;
        if (fault_) return;  // bypass hasta reset() o setAmount(0)
        hasProcessed_ = true;
        processCalls_.fetch_add(1, std::memory_order_relaxed);
        for (int off = 0; off < n; off += kChunk) {
            const int m = std::min(kChunk, n - off);
            processChunk(l + off, r + off, m);
            if (fault_) return;  // el chunk marcó fault: resto en bypass
        }
    }

    // ── Telemetría Ruta A (v2.5.380): evidencia de que el operador procesa
    // de verdad en el teléfono. Contadores atómicos lock-free (relaxed):
    // el hilo de audio solo hace fetch_add (sin locks, sin log, sin alloc)
    // y el hilo de control los lee para el log con límite de frecuencia
    // (ivanna_omega_jni.cpp: logOmniTelemetryThrottled, max 1 línea / 5 s).
    //   processCalls_   = llamadas a process() que entraron al operador
    //                     (preparado, punteros válidos, n>0, sin fault
    //                     latcheado). Incluye llamadas con amount=0.
    //   blocksRendered_ = chunks completados con mezcla wet/dry (paso 9);
    //                     con amount≈0 o fault el chunk retorna antes y NO
    //                     cuenta. Es el contador que demuestra procesado real.
    //   framesRendered_ = muestras de esos chunks (m por chunk).
    // Se ponen a cero en reset() (prepare() lo llama). El static_assert
    // guarda el invariante "sin locks en el hilo de audio" en ARM64/x86_64.
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "OmniField: se necesitan atomicos de 64 bits lock-free");
    std::atomic<std::uint64_t> processCalls_{0};
    std::atomic<std::uint64_t> blocksRendered_{0};
    std::atomic<std::uint64_t> framesRendered_{0};
    std::uint64_t processCalls() const noexcept {
        return processCalls_.load(std::memory_order_relaxed);
    }
    std::uint64_t blocksRendered() const noexcept {
        return blocksRendered_.load(std::memory_order_relaxed);
    }
    std::uint64_t framesRendered() const noexcept {
        return framesRendered_.load(std::memory_order_relaxed);
    }
    bool isActive() const noexcept { return appliedAmount_ > 0.001f && !fault_; }
    bool faulted() const noexcept { return fault_; }
    float appliedAmount() const noexcept { return appliedAmount_; }
    uint32_t designCount() const noexcept { return designCount_; }
    uint32_t totalMaaUpdates() const noexcept {
        return static_cast<uint32_t>(personalizer_.totalUpdates());
    }
    float sceneAzimuthDeg() const noexcept { return scene_.azimuthDeg(); }
    float sceneDiffuseness() const noexcept { return scene_.diffuseness(); }
    // Telemetría de convergencia: parámetro de orden r y fase media ψ del
    // campo de Kuramoto (ver OmniFieldConvergence.hpp).
    float convergenceR() const noexcept { return kuramoto_.orderR(); }
    float convergencePhase() const noexcept { return kuramoto_.meanPhase(); }
    // Bloqueado si r ≥ 0.90 y las diferencias de fase son estables
    // (ver KuramotoField::isLocked; solo r no basta).
    bool convergenceLocked() const noexcept { return kuramoto_.isLocked(); }
    // Latencia conservadora: base del renderer (15) + FIR (≈16). ORBE ya
    // aporta 512 muestras; la de OMNI es despreciable en la cadena.
    int latencySamples() const noexcept { return 31; }

private:
    static constexpr float kDeg2Rad = 0.017453292519943295f;
    static constexpr float kRad2Deg = 57.29577951308232f;

    static float wrap180(float deg) noexcept {
        float x = std::fmod(deg + 180.0f, 360.0f);
        if (x < 0.0f) x += 360.0f;
        return x - 180.0f;
    }

    // Sector MAA (0..7) para un azimut en grados: centros en 0,45,...,315.
    static int sectorOf(float azDeg) noexcept {
        const float w = wrap180(azDeg);
        int s = static_cast<int>(std::floor((w + 22.5f) / 45.0f));
        s %= 8;
        if (s < 0) s += 8;
        return s;
    }

    static float dbToLin(float db) noexcept {
        const float c = std::isfinite(db) ? std::clamp(db, -24.0f, 24.0f) : 0.0f;
        return std::pow(10.0f, c / 20.0f);
    }

    void processChunk(float* l, float* r, int m) noexcept {
        // 1. Entrada finita o fault (passthrough: no se tocan los buffers).
        for (int i = 0; i < m; ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) {
                fault_ = true;
                return;
            }
        }
        // 2. Parámetros saneados.
        const float target =
            std::isfinite(params_.amount) ? std::clamp(params_.amount, 0.0f, 1.0f) : 0.0f;
        const float pull = std::isfinite(params_.scenePull)
                               ? std::clamp(params_.scenePull, 0.0f, 1.0f)
                               : 0.3f;
        const float distScale = std::isfinite(params_.distanceM)
                                    ? std::clamp(params_.distanceM, 0.5f, 2.0f) / 1.2f
                                    : 1.0f;
        // 3. Rampa de enganche del amount (constante de tiempo 15 ms).
        const float alpha =
            1.0f - std::exp(-static_cast<float>(m) / (sampleRate_ * 0.015f));
        appliedAmount_ += (target - appliedAmount_) * alpha;
        if (appliedAmount_ <= 0.0001f) return;  // bypass bit-exacto
        // 4. Escena + refresco MAA (diezmado por sector dentro de update).
        scene_.update(l, r, m);
        personalizer_.update(blockIndex_++);
        const float thetaScene = scene_.azimuthDeg();
        const float psi = scene_.diffuseness();
        // 4.5 CONVERGENCIA Omega–SAFMUS (un solo sistema dinámico:
        // escena → K → Kuramoto → r/ψ → fusión + tono emergente).
        // INTEGRATION POINT (refinamiento Omega–SAFMUS): la ley de
        // acoplamiento refinada se conecta AQUÍ, sustituyendo la línea
        // K = K0·(1−Ψ) (ver OmniFieldConvergence.hpp, § PUNTO DE
        // INTEGRACIÓN). El campo KuramotoField no cambia.
        float pullEff = pull;
        if (params_.convergence) {
            const float k = KuramotoField::kCoupling0 * (1.0f - psi);
            kuramoto_.setCoupling(k);
            // Puerta de energía (fail-closed): RMS de entrada del bloque;
            // objetivo = rms²/(rms²+thr²), thr = 1e-3 (−60 dBFS). Con
            // entrada silenciosa la puerta es 0 y el tono emergente es
            // silencio: el operador jamás auto-oscila.
            double eIn = 0.0;
            for (int i = 0; i < m; ++i)
                eIn += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
            const float inRms =
                static_cast<float>(std::sqrt(eIn / static_cast<double>(2 * m)));
            const float gateTgt =
                (inRms * inRms) / (inRms * inRms + 1e-6f);
            energyGate_ += 0.25f * (gateTgt - energyGate_);
            // Avance por muestra + tono emergente e[n] = A4·r·sin(2ψ)·puerta
            // (2º armónico, 884 Hz con bloqueo; ver OmniFieldConvergence.hpp).
            const float rMean = kuramoto_.stepBlock(
                emergentMono_, m,
                KuramotoField::kEmergentGain * energyGate_);
            // r suavizado POR BLOQUE (no por muestra, sin jitter).
            rBlock_ += 0.3f * (rMean - rBlock_);
            // Fusión: con r→1 los stems se atraen al centroide (campo
            // coherente); con r→0 se quedan en sus posiciones canónicas.
            pullEff = pull * (0.5f + 0.5f * rBlock_);
        }
        // 5. Separación en 4 stems (suma = mid por construcción).
        separator_.separate(l, r, m, stems_[0], stems_[1], stems_[2], stems_[3]);
        // 6. Cuota de energía por stem (para la atracción hacia la escena).
        float w[kNumStems];
        float wSum = 0.0f;
        for (int k = 0; k < kNumStems; ++k) {
            double e = 0.0;
            for (int i = 0; i < m; ++i) e += static_cast<double>(stems_[k][i]) * stems_[k][i];
            w[k] = static_cast<float>(std::sqrt(e / static_cast<double>(m)));
            wSum += w[k];
        }
        const float wInv = (wSum > 1e-12f) ? 1.0f / wSum : 0.0f;
        // 7. Por stem: posición → personalización → (re)diseño FIR → render.
        for (int i = 0; i < m; ++i) {
            outL_[i] = 0.0f;
            outR_[i] = 0.0f;
        }
        const float yawDeg = params_.headTracking ? yawRad_ * kRad2Deg : 0.0f;
        for (int k = 0; k < kNumStems; ++k) {
            const float wk = w[k] * wInv;
            float theta = kStemAzDeg[k] +
                          pullEff * (thetaScene - kStemAzDeg[k]) * wk * (1.0f - psi);
            const int sector = sectorOf(theta);
            const float warp = personalizer_.warpDeg(sector);
            const float gain = dbToLin(personalizer_.gainDb(sector));
            const float thetaQ = wrap180(theta + warp - yawDeg);
            const float rk = kStemDistM[k] * distScale;
            // Rediseño solo si la posición se movió más que el MAA del sector.
            const float thr = personalizer_.redesignThresholdDeg(sector);
            if (!firValid_[k] || std::fabs(thetaQ - lastTheta_[k]) > thr ||
                std::fabs(rk - lastR_[k]) > 0.05f) {
                const float azR = thetaQ * kDeg2Rad;
                const float elR = kStemElDeg[k] * kDeg2Rad;
                const float dir[3] = {std::sin(azR) * std::cos(elR),
                                      std::cos(azR) * std::cos(elR), std::sin(elR)};
                BinauralFir fir;
                if (designer_.design(dir, rk, fir)) {
                    firs_[k] = fir;
                    renderers_[k].setFir(fir);
                    firValid_[k] = true;
                    lastTheta_[k] = thetaQ;
                    lastR_[k] = rk;
                    ++designCount_;
                } else if (!firValid_[k]) {
                    // Sin FIR válido y el diseño falló: FIR plano (el stem
                    // suena centrado) en vez de silenciarse. Fail-closed.
                    BinauralFir flat;
                    for (int t = 0; t < BinauralFir::kTaps; ++t) {
                        flat.l[t] = 0.0f;
                        flat.r[t] = 0.0f;
                    }
                    flat.l[0] = 1.0f;
                    flat.r[0] = 1.0f;
                    flat.delayL = 0.0f;
                    flat.delayR = 0.0f;
                    firs_[k] = flat;
                    renderers_[k].setFir(flat);
                    firValid_[k] = true;
                    lastTheta_[k] = thetaQ;
                    lastR_[k] = rk;
                }
                // Diseño fallido con FIR previo válido: se conserva el anterior.
            }
            renderers_[k].processMono(stems_[k], tmpL_, tmpR_, m);
            for (int i = 0; i < m; ++i) {
                outL_[i] += tmpL_[i] * gain;
                outR_[i] += tmpR_[i] * gain;
            }
        }
        // 7.5 Tono emergente como 5º stem por el MISMO camino
        // StemRenderer/FIR que los demás (miembro separado, no se extienden
        // los arrays de 4 stems). FIR en el azimut de escena (elevación 0,
        // distancia = distanceM), rediseñado cuando Θ se mueve más que el
        // umbral MAA del sector. Pasa por el mismo level-match y los mismos
        // checks de fault del paso 8 (se suma a outL/outR antes de ellos).
        if (params_.convergence && energyGate_ > 1e-4f) {
            const float thetaE = thetaScene;
            const int sectorE = sectorOf(thetaE);
            const float warpE = personalizer_.warpDeg(sectorE);
            const float gainE = dbToLin(personalizer_.gainDb(sectorE));
            const float thetaQE = wrap180(thetaE + warpE - yawDeg);
            const float rE = std::isfinite(params_.distanceM)
                                 ? std::clamp(params_.distanceM, 0.5f, 2.0f)
                                 : 1.2f;
            const float thrE = personalizer_.redesignThresholdDeg(sectorE);
            if (!emergentFirValid_ || std::fabs(thetaQE - emergentLastTheta_) > thrE ||
                std::fabs(rE - emergentLastR_) > 0.05f) {
                const float azR = thetaQE * kDeg2Rad;
                const float dir[3] = {std::sin(azR), std::cos(azR), 0.0f};
                BinauralFir fir;
                if (designer_.design(dir, rE, fir)) {
                    emergentFir_ = fir;
                    emergentRenderer_.setFir(fir);
                    emergentFirValid_ = true;
                    emergentLastTheta_ = thetaQE;
                    emergentLastR_ = rE;
                    ++designCount_;
                } else if (!emergentFirValid_) {
                    BinauralFir flat;
                    for (int t = 0; t < BinauralFir::kTaps; ++t) {
                        flat.l[t] = 0.0f;
                        flat.r[t] = 0.0f;
                    }
                    flat.l[0] = 1.0f;
                    flat.r[0] = 1.0f;
                    flat.delayL = 0.0f;
                    flat.delayR = 0.0f;
                    emergentFir_ = flat;
                    emergentRenderer_.setFir(flat);
                    emergentFirValid_ = true;
                    emergentLastTheta_ = thetaQE;
                    emergentLastR_ = rE;
                }
            }
            emergentRenderer_.processMono(emergentMono_, tmpL_, tmpR_, m);
            for (int i = 0; i < m; ++i) {
                outL_[i] += tmpL_[i] * gainE;
                outR_[i] += tmpR_[i] * gainE;
            }
        }
        // 8. Nivel: match suave y acotado hacia la energía de entrada, y
        // fault si el renderer produce (casi) silencio con entrada presente.
        double inE = 0.0, renE = 0.0;
        for (int i = 0; i < m; ++i) {
            inE += static_cast<double>(l[i]) * l[i] + static_cast<double>(r[i]) * r[i];
            renE += static_cast<double>(outL_[i]) * outL_[i] +
                    static_cast<double>(outR_[i]) * outR_[i];
        }
        inE /= static_cast<double>(2 * m);
        renE /= static_cast<double>(2 * m);
        if (inE > 1e-8 && renE < inE * 1e-6) {
            fault_ = true;
            return;  // no se tocan l/r: bypass
        }
        if (inE > 1e-12 && renE > 1e-12) {
            float tgt = static_cast<float>(std::sqrt(inE / renE));
            tgt = std::clamp(tgt, 0.5f, 2.0f);
            levelGain_ += (tgt - levelGain_) * 0.05f;
        }
        // 9. Mezcla wet/dry (el dry es la entrada original, aún intacta).
        const float a = appliedAmount_;
        const float g = levelGain_;
        for (int i = 0; i < m; ++i) {
            l[i] = (1.0f - a) * l[i] + a * outL_[i] * g;
            r[i] = (1.0f - a) * r[i] + a * outR_[i] * g;
        }
        // Chunk renderizado de verdad: cuenta para la telemetría Ruta A.
        blocksRendered_.fetch_add(1, std::memory_order_relaxed);
        framesRendered_.fetch_add(static_cast<std::uint64_t>(m),
                                 std::memory_order_relaxed);
    }

    float sampleRate_ = 48000.0f;
    bool prepared_ = false;
    bool hasProcessed_ = false;
    OmniParams params_;
    float appliedAmount_ = 0.0f;
    float yawRad_ = 0.0f;
    bool fault_ = false;
    uint32_t designCount_ = 0;
    int blockIndex_ = 0;
    float levelGain_ = 1.0f;

    StemSeparator separator_;
    SceneEstimator scene_;
    MaaPersonalizer personalizer_;
    FirDesigner designer_;
    KuramotoField kuramoto_;
    StemRenderer renderers_[kNumStems];
    StemRenderer emergentRenderer_;  // 5º stem: tono emergente (r·sin ψ)
    BinauralFir emergentFir_;
    bool emergentFirValid_ = false;
    float emergentLastTheta_ = 0.0f;
    float emergentLastR_ = 0.0f;
    float rBlock_ = 0.0f;    // r suavizado por bloque (fusión)
    float energyGate_ = 0.0f;  // puerta suave de energía de entrada
    BinauralFir firs_[kNumStems];
    bool firValid_[kNumStems] = {};
    float lastTheta_[kNumStems] = {};
    float lastR_[kNumStems] = {};

    float stems_[kNumStems][kChunk];
    float emergentMono_[kChunk];  // tono emergente por muestra (5º stem)
    float outL_[kChunk];
    float outR_[kChunk];
    float tmpL_[kChunk];
    float tmpR_[kChunk];
};

}  // namespace omnifield
}  // namespace ivanna
