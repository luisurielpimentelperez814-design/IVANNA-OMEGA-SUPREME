// gl_uniform_bridge_v2.hpp
/*
 * ============================================================================
 * IVANNA — Puente de uniforms GL v2: 13 bandas crudas para wallpaper PBR
 * ============================================================================
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Diferencia con gl_uniform_bridge.hpp (v1):
 *   v1 colapsa las 13 bandas del gammatone en 3 agregados (bass/mid/high)
 *   para el shader simple ya en producción. v2 NO colapsa: expone las 13
 *   bandas suavizadas + predichas individualmente, para que wallpaper_v2.glsl
 *   dibuje un anillo de 13 nodos con detalle espectral real en vez de 3
 *   agregados. v1 sigue intacto y en uso; v2 es aditivo.
 *
 * processBlockFromNPE(): entrada específica para la salida YA procesada por
 * IvannaNpeEngine (NHO + LIF + BiquadEnvelopeBank + AutonomousBrain), no el
 * audio crudo de captura. Wiring pendiente de la sesión anterior: el
 * wallpaper debe reaccionar a la señal después del realce neuromórfico, no
 * antes, para que el brillo/textura del wallpaper coincida con lo que
 * realmente se escucha (post-NPE), igual que ya hace v1 con el downmix
 * post-DSPBridge.
 * ============================================================================
 */
#pragma once
#include "gammatone_lattice.hpp"
#include "../include/audio_thread_priority.h"
#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>

namespace ivanna::vis {

// Mismas constantes de tau/floor/ceil que v1 (bien calibradas ya en producción),
// reutilizadas por banda individual en vez de por agregado.
struct BandSmootherV2 {
    float value = 0.f;
    inline void tick(float target, float dt, float attackTau, float releaseTau) noexcept {
        const float tau = (target > value) ? attackTau : releaseTau;
        const float coeff = 1.f - expf(-dt / tau);
        value += coeff * (target - value);
    }
};

struct BandPredictorV2 {
    std::array<float, 2> history{0.f, 0.f};
    inline float predictNext() const noexcept {
        static constexpr float a1 = 1.2f, a2 = -0.2f;
        return std::clamp(a1 * history[0] + a2 * history[1], 0.f, 1.f);
    }
    inline void update(float value) noexcept {
        history[1] = history[0];
        history[0] = value;
    }
};

namespace detail_v2 {
// Tau y rango dB por banda: interpolación lineal entre los valores graves
// (iguales a kBass* de v1) y agudos (iguales a kHigh* de v1), de forma que
// la banda 0 y la banda 12 coincidan exactamente con el comportamiento ya
// validado de v1 en sus extremos, y las intermedias interpolen suave.
// (Funciones libres, no miembro: un static constexpr miembro no puede
// llamarse a sí mismo — ni a otro miembro — antes de que la clase esté
// completa.)
constexpr std::array<float, GTL_BANDS> makeAttackTau() noexcept {
    std::array<float, GTL_BANDS> a{};
    for (int b = 0; b < GTL_BANDS; ++b) {
        const float t = static_cast<float>(b) / (GTL_BANDS - 1);
        a[b] = 0.020f + t * (0.002f - 0.020f);
    }
    return a;
}
constexpr std::array<float, GTL_BANDS> makeReleaseTau() noexcept {
    std::array<float, GTL_BANDS> a{};
    for (int b = 0; b < GTL_BANDS; ++b) {
        const float t = static_cast<float>(b) / (GTL_BANDS - 1);
        a[b] = 0.450f + t * (0.180f - 0.450f);
    }
    return a;
}
constexpr std::array<float, GTL_BANDS> makeFloorDb() noexcept {
    std::array<float, GTL_BANDS> a{};
    for (int b = 0; b < GTL_BANDS; ++b) {
        const float t = static_cast<float>(b) / (GTL_BANDS - 1);
        a[b] = -50.f + t * (-60.f - (-50.f));
    }
    return a;
}
constexpr std::array<float, GTL_BANDS> makeCeilDb() noexcept {
    std::array<float, GTL_BANDS> a{};
    for (int b = 0; b < GTL_BANDS; ++b) {
        const float t = static_cast<float>(b) / (GTL_BANDS - 1);
        a[b] = -6.f + t * (-14.f - (-6.f));
    }
    return a;
}
} // namespace detail_v2

class GLUniformBridgeV2 {
public:
    void init(float fs) noexcept {
        fb_.init(fs);
        fs_ = fs;
        ivanna::audio::enableAudioThreadFastMathOnce();
    }

    void setDeviceLatencyMs(float latencyMs) noexcept {
        deviceLatencyMs_ = std::max(0.f, latencyMs);
    }

    // Punto de entrada principal v2: recibe el mono downmix YA procesado por
    // IvannaNpeEngine (post neuromórfico), no el audio crudo de captura.
    inline void processBlockFromNPE(const float* __restrict__ npeMono, int n) noexcept {
        float bands[GTL_BANDS];
        fb_.process(npeMono, n, bands);

        const float dt = static_cast<float>(n) / fs_;
        const float attackScale = compensationAttackScale();

        for (int b = 0; b < GTL_BANDS; ++b) {
            const float norm = normalizeLog(bands[b], kFloorDb[b], kCeilDb[b]);
            smoothers_[b].tick(norm, dt, kAttackTau[b] * attackScale, kReleaseTau[b]);

            const float smoothedNorm = normalizeLog(smoothers_[b].value, kFloorDb[b], kCeilDb[b]);
            const float blended = std::clamp(
                0.7f * smoothedNorm + 0.3f * predictors_[b].predictNext(), 0.f, 1.f);
            predictors_[b].update(smoothedNorm);

            bandUniforms_[b].store(blended, std::memory_order_relaxed);
        }
        // Última banda con release semántico: marca el "punto de sincronía"
        // para el hilo GL (mismo patrón que v1: relaxed en las demás, release
        // en la última escritura del bloque).
        bandUniforms_[GTL_BANDS - 1].store(
            bandUniforms_[GTL_BANDS - 1].load(std::memory_order_relaxed),
            std::memory_order_release);
    }

    // sampleForRender: llamado desde el hilo GL, lock-free.
    inline void sampleForRender(float out[GTL_BANDS]) const noexcept {
        for (int b = 0; b < GTL_BANDS - 1; ++b) {
            out[b] = bandUniforms_[b].load(std::memory_order_relaxed);
        }
        out[GTL_BANDS - 1] = bandUniforms_[GTL_BANDS - 1].load(std::memory_order_acquire);
    }

    void reset() noexcept {
        for (int b = 0; b < GTL_BANDS; ++b) {
            smoothers_[b] = BandSmootherV2{};
            predictors_[b] = BandPredictorV2{};
            bandUniforms_[b].store(0.f, std::memory_order_relaxed);
        }
    }

private:
    inline float compensationAttackScale() const noexcept {
        constexpr float kReferenceLatencyMs = 30.0f;
        if (deviceLatencyMs_ <= kReferenceLatencyMs) return 1.0f;
        const float extra = deviceLatencyMs_ - kReferenceLatencyMs;
        return 1.0f - std::clamp(extra / 200.0f, 0.0f, 0.6f);
    }

    static float normalizeLog(float linEnergy, float floorDb, float ceilDb) noexcept {
        const float db = 20.f * log10f(std::max(linEnergy, 1e-6f));
        return std::clamp((db - floorDb) / (ceilDb - floorDb), 0.f, 1.f);
    }

    static constexpr std::array<float, GTL_BANDS> kAttackTau = detail_v2::makeAttackTau();
    static constexpr std::array<float, GTL_BANDS> kReleaseTau = detail_v2::makeReleaseTau();
    static constexpr std::array<float, GTL_BANDS> kFloorDb = detail_v2::makeFloorDb();
    static constexpr std::array<float, GTL_BANDS> kCeilDb = detail_v2::makeCeilDb();

    GammatoneLattice13 fb_;
    float fs_ = 48000.f;
    float deviceLatencyMs_ = 0.f;
    std::array<BandSmootherV2, GTL_BANDS> smoothers_;
    std::array<BandPredictorV2, GTL_BANDS> predictors_;
    std::array<std::atomic<float>, GTL_BANDS> bandUniforms_{};
};

} // namespace ivanna::vis
