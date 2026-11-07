// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// HolosVolterraKernel — K_b: el kernel de Volterra por banda de HOLOS.
//
// FUSION.md §4.4.2: todo generador de armónicos del motor es un término de
// una serie de Volterra; el kernel con memoria (Volterra H2) es la
// generalización y los demás son sus casos degenerados sin memoria.
// HOLOS evalúa UN solo kernel:
//
//   K[x] = h1·x + h2·x² + h3·x³ (memoryless, por banda) + H2_memoria[x]
//
// DESVIACIÓN DOCUMENTADA DEL DISEÑO (§4.4.2 decía "polinomio por bin"):
// un polinomio aplicado a bins complejos de la STFT NO genera armónicos
// en 2f/3f — solo modula la magnitud/fase del propio bin. La generación
// de armónicos es intermodulación en el dominio temporal y solo existe
// ahí. Por eso K se evalúa en el dominio temporal, sobre la señal ya
// renderizada por G (post-OLA), no por bin: el estimador de escena ve la
// señal limpia y la generación de armónicos no contamina θ_b. El término
// con memoria (Volterra H2, banda ancha) ya estaba "antes de la STFT" en
// el propio diseño; aquí ambos términos viven en el mismo sitio temporal.
// La degeneración Exciter/GoldenEar→K se prueba en el dominio temporal
// (tests/test_holos_volterra_kernel.cpp), que es donde esos módulos viven.
//
// Contenido de K (una sola mezcla, una sola corrección):
// - Cheb T2/T3 (Exciter, >3 kHz): misma matemática que ChebHarmonicShaper
//   (k2=0.42·w·dEff, k3=0.42·(1−0.85w)·dEff, saturación suave >0.85,
//   DC-block 15 Hz). Sin oversampling 2× (FUSION §7.2: el aliasing lo
//   controla la máscara de banda, como el pre-LPF 8 kHz).
// - GoldenEar H2 (<8 kHz): 2·lf² con DC por EMA (τ≈500 muestras).
// - Padé [3/2] (softClip): la misma racional del Exciter y del Manifold
//   (idénticas); un solo polinomio cúbico reemplaza los tres racionales.
// - Mezcla única: 0.65·Cheb + 0.35·Padé + 0.12·H2golden, con UNA sola
//   corrección de regresión (+7.5 dB, EMA 3 ms), no dos.
// - Memoria OHC coclear (Heun 10/80 ms): la envolvente Heun gobierna un
//   h2 variante en el tiempo (k2Scale = 1 − 0.5·nl, nl = 0.085·env² +
//   0.05·env clamp [0, 0.45] — la forma funcional del modelo coclear
//   §1.7). Sin kernel medido, K = polinomio memoryless + Heun.
// - Memoria Volterra H2 (K=64, triangular, puerta IVK1): se reutiliza
//   volterra_h2_symmetric SIN reescribirlo. Sin kernel → identidad
//   (no se llama: coste cero). La identificación de kernel por banda
//   (disperso, ~10% no nulos) es fase 2; no se inventa ningún kernel.
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "../include/dsp_types.h"

namespace ivanna {
namespace holos {

struct VolterraKernelParams {
    float harmonics = 0.32f;  // 0..1, mezcla del término K (wet 0.32 del Exciter)
    float warmth = 0.5f;      // 0..1, balance par/impar (warmth del Exciter)
    float driveNorm = 0.45f;  // 0..1, drive normalizado (DSPParams.drive)
    bool h3Enabled = true;    // false en tier térmico WARM (solo H2)
};

class HolosVolterraKernel {
public:
    // Pimpl (VolterraImpl es incompleto aquí): constructor y destructor se
    // declaran y se definen en el .cpp, donde el tipo está completo. Sin
    // esto, el constructor/destructor implícitos se instancian en cada TU
    // que crea o destruye un kernel y exigen el tipo completo (error de
    // compilación en los tests host). La clase es no-copiable y no-movible
    // (atomics + unique_ptr): el operador la contiene por valor y nunca
    // la mueve.
    HolosVolterraKernel();
    ~HolosVolterraKernel();
    HolosVolterraKernel(const HolosVolterraKernel&) = delete;
    HolosVolterraKernel& operator=(const HolosVolterraKernel&) = delete;

    static constexpr float kExciterCeiling = 0.98855f;  // escala de salida del Exciter

    // Polinomio de Chebyshev sin DC-block (puro, para tests): la misma
    // fórmula que ChebHarmonicShaper::tick sin el bloqueador DC.
    // u: muestra (ya escalada por driveLin = 1+3·driveNorm si se quiere
    // reproducir el Exciter); drive: [0,1] normalizado; k2Scale: 1.0
    // normal, <1.0 cuando la OHC lo atenúa.
    static float chebPolyRaw(float u, float drive, float warmth, float f8 = 0.0f,
                             float k2Scale = 1.0f) noexcept {
        if (!std::isfinite(u)) return 0.0f;
        const float d = std::clamp(drive, 0.0f, 1.0f);
        const float w = std::clamp(warmth, 0.0f, 1.0f);
        const float f = std::clamp(f8, 0.0f, 1.0f);
        const float dEff = d * (1.0f - 0.5f * f);
        // Saturación suave C¹ para |u| > 0.85 (misma que el shaper).
        float uu = u;
        const float ax = std::fabs(u);
        if (ax > 0.85f) {
            const float ex = ax - 0.85f;
            const float sat = 0.85f + 0.15f * (ex / (0.15f + ex));
            uu = (u >= 0.0f) ? sat : -sat;
        }
        const float k2 = 0.42f * w * dEff * k2Scale;
        const float k3 = 0.42f * (1.0f - 0.85f * w) * dEff;
        const float t2 = uu * uu;                  // T2*(u) = u² (par puro)
        const float t3 = 4.0f * uu * uu * uu - 3.0f * uu;  // T3(u)
        return (1.0f - k2 - k3) * uu + k2 * t2 + k3 * (t3 * (-0.33333333f));
    }

    // Padé [3/2] del Exciter (softClip), misma racional que el Manifold.
    // drive: drive LINEAL (1+3·driveNorm), como lo llama el Exciter.
    static float softClipPade(float x, float driveLin) noexcept {
        if (!std::isfinite(x)) return 0.0f;
        float v = x * driveLin;
        const float av = std::fabs(v);
        if (av > 3.0f) {
            v = (v > 0.0f) ? (3.0f + 0.5f * std::tanh((v - 3.0f) * 0.5f))
                           : (-3.0f - 0.5f * std::tanh((-v - 3.0f) * 0.5f));
        }
        const float v2 = v * v;
        return v * (1.0f + v2 * 0.037037f) / (1.0f + v2 * 0.333333f);
    }

    void prepare(float sr);
    void reset() noexcept;
    void setParams(const VolterraKernelParams& p) noexcept;

    // Procesa n muestras in-place (L/R). Con harmonics=0 y sin kernel
    // Volterra es identidad exacta de coste cero (no toca los buffers).
    // Cero alloc en process().
    void process(float* l, float* r, int n) noexcept;

    // Memoria Volterra H2 (K=64, puerta IVK1): solo existe si hay kernel
    // cargado. Sin kernel → identidad (process() no lo llama).
    bool hasVolterraKernel() const noexcept;
    // Carga de kernel (hilo de CONTROL, nunca el de audio). Passthrough a
    // VolterraH2Symmetric::loadKernelPackage. Fase 2 (no hay kernel medido).
    int loadKernelPackage(const uint8_t* package, size_t packageSize) noexcept;

    // Diagnóstico / tests.
    float heunEnvelope(int ch) const noexcept;
    float fundGain(int ch) const noexcept;
    float sampleRate() const noexcept { return sampleRate_; }

private:
    struct Channel {
        Biquad hpf;      // >3 kHz (máscara Exciter)
        Biquad lpf;      // <8 kHz (pre-LPF compartido)
        Biquad lpfGold;  // <8 kHz (camino GoldenEar)
        // DC-block 15 Hz del shaper Cheb.
        float dcR = 0.99804f, dcX = 0.0f, dcY = 0.0f;
        // EMA de DC del término H2 GoldenEar (τ≈500 muestras).
        float h2Mean = 0.0f;
        // Regresión de fundamental (EMA 3 ms): corrige la ganancia lineal
        // del shaper (el +7.5 dB del Exciter), aplicada UNA vez.
        float fundCross = 0.0f, fundPow = 0.0f, fundGain = 0.0f;
        // Envolvente OHC Heun (10 ms ataque / 80 ms release, RK2).
        float heunEnv = 0.0f;
        float heunAtt = 0.0f, heunRel = 0.0f;
    };
    float tickChannel(float x, Channel& c) noexcept;

    float sampleRate_ = 48000.0f;
    bool prepared_ = false;
    std::atomic<float> harmonics_{0.32f};
    std::atomic<float> warmth_{0.5f};
    std::atomic<float> driveNorm_{0.45f};
    std::atomic<bool> h3Enabled_{true};
    Channel ch_[2];
    float fundCoef_ = 0.0f;  // EMA 3 ms (regresión)

    // Volterra H2 con memoria (banda ancha, K=64). Se crea en prepare();
    // sin kernel es identidad y process() ni lo toca.
    struct VolterraImpl;
    std::unique_ptr<VolterraImpl> volterra_;
    std::vector<float> interleave_;  // scratch preasignado (2×512)
};

}  // namespace holos
}  // namespace ivanna
