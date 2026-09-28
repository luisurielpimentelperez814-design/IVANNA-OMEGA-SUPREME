// Tests REALES contra SafetyLimiter y HarmonicExciter — llaman al código
// real de IVANNA (app/src/main/cpp/include/{SafetyLimiter,HarmonicExciter}.h),
// no simulaciones locales.
//
// Reemplaza tres archivos falsificados de app/src/test/cpp/ (verificado
// leyéndolos completos — ninguno incluía un header real de IVANNA):
//   - peak_guard_regression_test.cpp      (aplicaba std::tanh() a un vector
//     de constantes 2.0f — no llamaba a ningún limiter real)
//   - test_peak_guard_regression.cpp      (duplicado del anterior)
//   - harmonic_exciter_overshoot_regression_test.cpp (fórmula cúbica local
//     x - x^3/3, sin relación con HarmonicExciter real)
//
// Documentado en AGENT_CLAIMS.md bajo "Suite de tests C++ huérfana y
// falsificada — app/src/test/cpp/".

#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "SafetyLimiter.h"
#include "HarmonicExciter.h"
#include "dsp_types.h"

using ivanna::SafetyLimiter;
using ivanna::HarmonicExciter;
using ivanna::DSPParams;

namespace {

// Tono real a 1kHz — no una constante ni un vector uniforme.
std::vector<float> makeSine(int frames, float amplitude, float freqHz = 1000.f,
                             float sr = 48000.f) {
    std::vector<float> v(frames);
    for (int i = 0; i < frames; ++i)
        v[i] = amplitude * std::sin(2.0f * 3.14159265f * freqHz * i / sr);
    return v;
}

} // namespace

// ── SafetyLimiter ────────────────────────────────────────────────────────

// FIX real que esto verifica: el ceiling documentado en el header es
// -0.1 dBFS (~0.98855 lineal). Una señal real a amplitud 3.0 (muy por
// encima de cualquier ceiling razonable) NUNCA debe salir por encima de
// eso — a diferencia del fake que probaba tanh(2.0) contra sí mismo, esto
// falla de verdad si alguien rompe el limiter.
TEST(SafetyLimiterReal, CeilingNeverExceeded) {
    SafetyLimiter lim;
    lim.setSampleRate(48000.f);
    lim.setParams(0.63096f, 0.98855f); // defaults documentados en el header
    auto L = makeSine(4096, 3.0f);
    auto R = makeSine(4096, 3.0f, 1000.f, 48000.f);
    lim.process(L.data(), R.data(), static_cast<int>(L.size()));
    constexpr float kCeilingWithEps = 0.98855f + 1e-4f; // tolerancia float
    for (size_t i = 0; i < L.size(); ++i) {
        ASSERT_LE(std::fabs(L[i]), kCeilingWithEps) << "muestra L[" << i << "]";
        ASSERT_LE(std::fabs(R[i]), kCeilingWithEps) << "muestra R[" << i << "]";
    }
}

// getClipCount() es telemetría real (std::atomic<int> en el header) que
// EnginesStatusScreen/OemViewModel exponen al usuario — si el limiter no
// cuenta clips de verdad, el usuario ve "0 clips" con audio saturado.
TEST(SafetyLimiterReal, ClipCountReflectsRealOverThreshold) {
    SafetyLimiter lim;
    lim.setSampleRate(48000.f);
    lim.resetClipCount();
    ASSERT_EQ(lim.getClipCount(), 0);
    auto L = makeSine(8192, 5.0f); // muy por encima del threshold
    auto R = L;
    lim.process(L.data(), R.data(), static_cast<int>(L.size()));
    EXPECT_GT(lim.getClipCount(), 0)
        << "Señal muy por encima del threshold no incrementó el contador real de clips";
}

// El limiter no debe tocar el silencio: ni ganancia reducida ni ruido de
// piso introducido. Reemplaza la premisa (nunca verificada) de varios
// fakes de que "silencio produce silencio".
TEST(SafetyLimiterReal, SilenceStaysSilentAndUnreduced) {
    SafetyLimiter lim;
    lim.setSampleRate(48000.f);
    std::vector<float> L(2048, 0.0f), R(2048, 0.0f);
    lim.process(L.data(), R.data(), static_cast<int>(L.size()));
    for (float v : L) ASSERT_EQ(v, 0.0f);
    EXPECT_LT(lim.getGainReduction(), 0.01f)
        << "Silencio real no debería producir reducción de ganancia";
}

// Bypass real: cuando bypass(true), la salida debe ser BIT-EXACTA a la
// entrada. Esto es lo que dsp_bypass_regression_test.cpp (fake) afirmaba
// probar con "output = input" — una asignación local, no una llamada al
// bypass real del limiter.
TEST(SafetyLimiterReal, BypassIsTrueBitExactBypass) {
    SafetyLimiter lim;
    lim.setSampleRate(48000.f);
    lim.bypass(true);
    auto L = makeSine(2048, 2.5f);
    auto R = makeSine(2048, 2.5f);
    const auto Lref = L, Rref = R;
    lim.process(L.data(), R.data(), static_cast<int>(L.size()));
    for (size_t i = 0; i < L.size(); ++i) {
        ASSERT_EQ(L[i], Lref[i]) << "bypass real alteró la muestra L[" << i << "]";
        ASSERT_EQ(R[i], Rref[i]) << "bypass real alteró la muestra R[" << i << "]";
    }
}

// El header documenta release de 50ms — no bombeo. Un transiente fuerte
// seguido de silencio debe ver caer getGainReduction() hacia 0 con el
// tiempo, no quedarse clavado en la reducción máxima.
TEST(SafetyLimiterReal, GainReductionRecoversAfterTransient) {
    SafetyLimiter lim;
    lim.setSampleRate(48000.f);
    auto hot = makeSine(512, 4.0f);
    std::vector<float> hotR = hot;
    lim.process(hot.data(), hotR.data(), static_cast<int>(hot.size()));
    const float reductionRightAfter = lim.getGainReduction();
    ASSERT_GT(reductionRightAfter, 0.01f) << "El transiente fuerte no generó reducción medible";

    // ~100ms de silencio a 48kHz = 4800 muestras — bastante más que el
    // release de 50ms documentado para que la ganancia se recupere.
    std::vector<float> silL(4800, 0.0f), silR(4800, 0.0f);
    lim.process(silL.data(), silR.data(), static_cast<int>(silL.size()));
    EXPECT_LT(lim.getGainReduction(), reductionRightAfter * 0.5f)
        << "getGainReduction() no se recuperó tras 100ms de silencio — "
        << "¿el release de 50ms sigue funcionando?";
}

// ── HarmonicExciter ──────────────────────────────────────────────────────

// El propio header documenta el bug real ya corregido: "medido: 1.52 con
// drive=16, wet=1.0 sobre onda cuadrada". Reproducimos ese caso exacto con
// el exciter REAL y verificamos que excScaleL_/R_ (el fix de headroom)
// sigue conteniendo el overshoot — a diferencia del fake, que aplicaba una
// fórmula cúbica inventada sin relación con esta clase.
TEST(HarmonicExciterReal, DoesNotOvershootWithHotDriveOnSquareWave) {
    HarmonicExciter exc;
    DSPParams p;
    p.drive = 16.0f;
    p.wet   = 1.0f;
    p.sampleRate = 48000;
    exc.setParams(p);

    // Onda cuadrada real (no un vector de constantes): +1/-1 alternando
    // cada 24 muestras — el caso exacto que el header cita como el que
    // midió 1.52 de overshoot antes del fix de excScale_.
    constexpr int N = 2048;
    std::vector<float> L(N), R(N);
    for (int i = 0; i < N; ++i) {
        L[i] = ((i / 24) % 2 == 0) ? 1.0f : -1.0f;
        R[i] = L[i];
    }
    exc.process(L.data(), R.data(), N);

    // Margen de seguridad razonable: el fix apunta a respetar headroom,
    // no a exactamente 1.0 (el header describe attack inmediato / release
    // suave de ~20ms, así que algo de overshoot transitorio es esperable,
    // pero NO el 1.52 medido antes del fix).
    for (int i = 0; i < N; ++i) {
        ASSERT_LT(std::fabs(L[i]), 1.2f)
            << "Overshoot del exciter en muestra " << i
            << " — ¿regresó el bug de excScale_ documentado en el header?";
    }
}

// El header documenta un contrato explícito: wet=0 debe ser bypass
// bit-exacto (wetNow_ arranca en 0.0 específicamente por esto, con una
// nota de que 0.5 "nunca se disparaba" y wet=0 alteraba la señal ~6.7e-4
// antes del fix). Verificamos ese contrato documentado contra el código real.
TEST(HarmonicExciterReal, ZeroWetIsTransparent) {
    HarmonicExciter exc;
    DSPParams p;
    p.drive = 8.0f;
    p.wet   = 0.0f;
    p.sampleRate = 48000;
    exc.setParams(p);

    auto L = makeSine(1024, 0.6f);
    auto R = makeSine(1024, 0.6f);
    const auto Lref = L, Rref = R;
    exc.process(L.data(), R.data(), static_cast<int>(L.size()));

    // Tolerancia más estricta que el error histórico documentado (~6.7e-4)
    // — si esto falla con un valor cercano a 6.7e-4, es la regresión exacta
    // que el header describe.
    for (size_t i = 0; i < L.size(); ++i) {
        ASSERT_NEAR(L[i], Lref[i], 1e-5f) << "muestra " << i;
    }
}

// setRuntimeReduction(1.0) debe silenciar el efecto (wet efectivo -> 0)
// sin tocar drive_ (el timbre) — documentado explícitamente en el header
// como el contrato del cierre del lazo adaptativo (FASE 4C).
TEST(HarmonicExciterReal, RuntimeReductionOneMutesEffectNotDrive) {
    HarmonicExciter exc;
    DSPParams p;
    p.drive = 10.0f;
    p.wet   = 1.0f;
    p.sampleRate = 48000;
    exc.setParams(p);
    exc.setRuntimeReduction(1.0f); // reducción total documentada

    auto L = makeSine(2048, 0.6f);
    auto R = makeSine(2048, 0.6f);
    const auto Lref = L, Rref = R;
    exc.process(L.data(), R.data(), static_cast<int>(L.size()));

    // Con reducción runtime al máximo, la salida debe converger hacia la
    // señal seca (dry) — no exactamente igual por el suavizado wetNow_
    // documentado (~15ms), pero mucho más cerca del dry que sin reducción.
    float maxDiff = 0.f;
    for (size_t i = L.size() / 2; i < L.size(); ++i) // segunda mitad: ya convergió
        maxDiff = std::max(maxDiff, std::fabs(L[i] - Lref[i]));
    EXPECT_LT(maxDiff, 0.05f)
        << "setRuntimeReduction(1.0) no silenció el efecto tras converger — "
        << "diff máxima vs dry: " << maxDiff;
}
