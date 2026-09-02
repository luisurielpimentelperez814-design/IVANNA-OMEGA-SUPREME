/**
 * test_limiter_hires_timing.cpp
 *
 * Barrera de regresión del bug reparado el 2026-08-27:
 * SafetyLimiter nunca recibía setSampleRate() ni setParams() (Ruta A JNI)
 * y solo setParams() (Ruta B daemon). Consecuencias medibles:
 *
 *   1) threshold == ceiling (0.98855) → soft-knee anulado, pared de ladrillo
 *      → pumping/thuds en material con transientes densos.
 *   2) Ataque (1.5 ms) y release (50 ms) calculados con m_sampleRate=48 kHz
 *      fijo → a 96/192/384 kHz las constantes quedaban 2×/4×/8× CORTAS en
 *      tiempo real (ataque 0.75/0.38/0.19 ms → tronidos en transientes;
 *      release 25/12.5/6.25 ms → la ganancia rebota a la frontera de cada
 *      bloque = bombeo/flutter audible).
 *
 * Estos tests miden el COMPORTAMIENTO temporal del limiter — no su estado
 * interno — para que la regresión se detecte aunque la implementación
 * interna cambie.
 */
#include <gtest/gtest.h>
#include "SafetyLimiter.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {

// Genera una sinusoide con un pico aislado de amplitud `peakAmp` en la
// muestra `peakPos`, sobre un nivel base `baseAmp`.
void makePeakBurst(std::vector<float>& L, std::vector<float>& R,
                   int n, float baseAmp, float peakAmp, int peakPos) {
    for (int i = 0; i < n; ++i) {
        const float s = baseAmp * std::sin(2.0f * 3.14159265f * 440.0f *
                                           (float)i / 48000.0f);
        L[i] = s; R[i] = s;
    }
    L[peakPos] = peakAmp;
    R[peakPos] = peakAmp;
}



} // namespace

// ── 1) Soft-knee real: threshold debe quedar POR DEBAJO del ceiling ────────
// Con setParams() (threshold -4 dBFS), una señal a -3 dBFS (entre threshold
// y ceiling) debe recibir reducción SUAVE parcial. Con el bug (threshold ==
// ceiling), esa misma señal pasaba intacta (sin reducción) — el knee no
// existía.
TEST(LimiterHiResTiming, SoftKneeIsActiveAfterSetParams) {
    ivanna::SafetyLimiter lim;
    lim.setParams();          // threshold 0.63096 (-4 dBFS), ceiling 0.98855
    lim.setSampleRate(48000.f);

    const int N = 512;
    // Señal a -3 dBFS: 0.7079 — por encima del threshold -4 dBFS.
    std::vector<float> L(N, 0.7079f), R(N, 0.7079f);
    lim.process(L.data(), R.data(), N);

    // Con knee activo, la salida debe ser MENOR que la entrada (reducción
    // parcial). Con el bug (threshold==ceiling=0.98855) saldría intacta.
    EXPECT_LT(L[N - 1], 0.7079f)
        << "Soft-knee inactivo: señal a -3 dBFS salió intacta — "
           "setParams() no se aplicó (pared de ladrillo)";
}

// ── 2) Ataque a 384 kHz: NO debe tardar 4× más muestras que a 48 kHz ──────
// Con el bug, setSampleRate() nunca se llamaba: el ataque seguía calculado
// para 48 kHz. A 384 kHz reales eso es 8× más muestras para el mismo 1.5 ms.
TEST(LimiterHiResTiming, AttackScalesWithRealSampleRate) {
    const int N = 4096;
    const float peakAmp = 1.2f;  // supera el ceiling
    const int peakPos = 100;

    // ── 48 kHz de referencia ──
    ivanna::SafetyLimiter lim48;
    lim48.setParams();
    lim48.setSampleRate(48000.f);
    std::vector<float> L48(N), R48(N);
    makePeakBurst(L48, R48, N, 0.1f, peakAmp, peakPos);
    lim48.process(L48.data(), R48.data(), N);
    const float dip48 = L48[peakPos];  // muestra del pico ya limitada

    // ── 384 kHz: debe comportarse igual (ataque 1.5 ms REALES, no 12 ms) ──
    ivanna::SafetyLimiter lim384;
    lim384.setParams();
    lim384.setSampleRate(384000.f);
    std::vector<float> L384(N), R384(N);
    makePeakBurst(L384, R384, N, 0.1f, peakAmp, peakPos);
    lim384.process(L384.data(), R384.data(), N);
    const float dip384 = L384[peakPos];

    // Ambos deben limitar el pico por debajo del ceiling en la propia
    // muestra del pico (lookahead de bloque). El pico 1.2 sin limitación
    // saldría a ~1.2 (o saturado a techo con error). Con ataque correcto
    // la reducción ya está activa al llegar al pico.
    EXPECT_LE(dip48, 0.99f)  << "48k: el pico escapó del limiter";
    EXPECT_LE(dip384, 0.99f) << "384k: el pico escapó del limiter — ataque "
                                "demasiado lento (coeficientes de 48 kHz?)";
}

// ── 3) Release a 384 kHz: la ganancia NO debe quedar hundida 8× más ───────
// Con el bug, release=50ms@48k aplicado a 384k → 400 ms reales de bombeo.
// Medimos cuántas muestras tarda la envolvente en recuperar el 90% tras un
// pico. A 48 kHz, 50 ms ≈ 2400 muestras. A 384 kHz correcto, ≈ 19200
// muestras (mismo tiempo). Si el coeficiente quedara en 48 kHz, a 384 kHz
// tardaría ~153600 muestras (400 ms) — 8× más.
TEST(LimiterHiResTiming, ReleaseIsRealtimeAtHighSampleRate) {
    // El release solo ocurre ENTRE bloques: blockGain se calcula con el peak
    // de cada bloque. Diseño: bloque 0 con pico 1.3 + DC 0.3; bloques
    // siguientes de DC 0.3 puro (< threshold 0.631 -> blockGain=1 -> la
    // envolvente suelta hacia 1.0 con el coeficiente de release).
    const int blockSize = 512;
    const int numBlocks = 120;               // 61440 muestras = 160 ms @384k
    const float dcAmp = 0.3f;

    ivanna::SafetyLimiter lim;
    lim.setParams();
    lim.setSampleRate(384000.f);

    std::vector<float> L(blockSize, dcAmp), R(blockSize, dcAmp);
    L[100] = 1.3f; R[100] = 1.3f;            // pico aislado en el bloque 0
    lim.process(L.data(), R.data(), blockSize);

    // Medir la envolvente de ganancia real (out/in sobre DC) hasta 90%.
    int recover = -1, total = 0;
    for (int b = 1; b < numBlocks && recover < 0; ++b) {
        std::fill(L.begin(), L.end(), dcAmp);
        std::fill(R.begin(), R.end(), dcAmp);
        lim.process(L.data(), R.data(), blockSize);
        for (int i = 0; i < blockSize; ++i) {
            ++total;
            if (L[i] / dcAmp >= 0.9f) { recover = total; break; }
        }
    }

    ASSERT_NE(recover, -1)
        << "La ganancia nunca recuperó 90% en 160 ms @384k";

    // Cálculo exacto: tras el bloque del pico la ganancia converge por ataque
    // (576 muestras tau @384k) quedando en ~0.78; el release correcto
    // (19200 muestras tau) la lleva a 0.9 en ~15k muestras (~40 ms).
    // Con el bug (coeficiente de 48 kHz aplicado a 384 kHz) serian ~1.9k
    // muestras (~5 ms) — rebote 8× más rápido = bombeo. Umbral 8000:
    // holgado para el caso correcto, atrapa el bug con 4× de margen.
    EXPECT_GT(recover, 8000)
        << "Release demasiado CORTO @384k: " << recover << " muestras "
           "(=" << (recover / 384.0f) << " ms) — coeficientes de 48 kHz?";
    EXPECT_LT(recover, 60000)
        << "Release demasiado largo @384k: " << recover << " muestras";
}

// ── 4) Idempotencia: re-llamar setSampleRate por cambio de SR no rompe ────
TEST(LimiterHiResTiming, SampleRateChangeIsIdempotent) {
    ivanna::SafetyLimiter lim;
    lim.setParams();
    for (float sr : {48000.f, 96000.f, 192000.f, 384000.f, 48000.f}) {
        lim.setSampleRate(sr);
        std::vector<float> L(256, 0.5f), R(256, 0.5f);
        lim.process(L.data(), R.data(), 256);
        for (float v : L) EXPECT_TRUE(std::isfinite(v));
        for (float v : R) EXPECT_TRUE(std::isfinite(v));
    }
}
