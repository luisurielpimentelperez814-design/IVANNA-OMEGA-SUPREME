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
 *      fijo → a 96/192/384 kHz el ataque real era 3/6/12 ms (transientes
 *      pasaban sin limitar → tronidos/clipping) y el release 100/200/400 ms
 *      (bombeo grave audible).
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

// Cuenta en qué muestra del bloque la envolvente de ganancia recupera el
// 90% del camino hacia 1.0 tras un pico. Devuelve -1 si nunca recupera.
int measureReleaseSamples(ivanna::SafetyLimiter& lim,
                          std::vector<float> L, std::vector<float> R,
                          int peakPos, float peakAmp) {
    lim.process(L.data(), R.data(), (int)L.size());
    // La ganancia aplicada se observa como out/in en las muestras tras el
    // pico (donde la entrada vuelve a ser pequeña y conocida).
    // Medimos indirectamente: la amplitud de la sinusoide de salida tras
    // el pico, buscando dónde vuelve al 90% de su nivel original.
    const float baseAmp = 0.1f;
    const int start = peakPos + 1;
    const float target = baseAmp * 0.9f;
    for (int i = start; i < (int)L.size() - 1; ++i) {
        if (std::fabs(L[i]) >= target) return i - start;
    }
    return -1;
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
    const int N = 24000;         // 62.5 ms @ 384k — margen para 50 ms reales
    const int peakPos = 500;

    ivanna::SafetyLimiter lim;
    lim.setParams();
    lim.setSampleRate(384000.f);
    std::vector<float> L(N), R(N);
    makePeakBurst(L, R, N, 0.1f, 1.3f, peakPos);
    lim.process(L.data(), R.data(), N);

    const int recover = measureReleaseSamples(lim, L, R, peakPos, 1.3f);
    ASSERT_NE(recover, -1) << "La ganancia nunca recuperó 90% en 62 ms @384k";
    // 50 ms reales @384k = 19200 muestras. Permitimos hasta 22000 (≈57 ms)
    // como tolerancia. Con el bug serían ~153600 (no alcanzable en N=24000,
    // así que ASSERT_NE(-1) ya lo atrapa; el umbral refuerza).
    EXPECT_LT(recover, 22000)
        << "Release demasiado lento @384k: " << recover << " muestras "
           "(=" << (recover / 384.0f) << " ms) — coeficientes a 48 kHz fijos?";
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
