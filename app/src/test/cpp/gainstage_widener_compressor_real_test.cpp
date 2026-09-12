// Tests REALES contra GainStage, StereoWidener y Compressor — código real
// de IVANNA, no simulaciones. Ver metodología y hallazgos completos en
// AGENT_CLAIMS.md bajo "Suite de tests C++ huérfana y falsificada".

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include "GainStage.h"
#include "StereoWidener.h"
#include "Compressor.h"
#include "dsp_types.h"

using ivanna::GainStage;
using ivanna::StereoWidener;
using ivanna::Compressor;
using ivanna::DSPParams;

namespace {
std::vector<float> makeSine(int frames, float amplitude, float freqHz, float sr = 96000.f) {
    std::vector<float> v(frames);
    for (int i = 0; i < frames; ++i)
        v[i] = amplitude * std::sin(2.0f * 3.14159265f * freqHz * i / sr);
    return v;
}
} // namespace

// ── GainStage ────────────────────────────────────────────────────────────

// Contrato documentado explícitamente en el header: setRuntimeGain "solo
// puede ATENUAR, nunca amplificar. Cap máximo = 1.0". Pedimos 3.0 (muy por
// encima) y verificamos que la señal de salida real, tras converger, NUNCA
// supera lo que produciría con ganancia de runtime 1.0 (sin amplificación).
TEST(GainStageReal, RuntimeGainNeverAmplifiesAboveUnity) {
    GainStage gs;
    DSPParams p; p.master = 0.0f; p.sampleRate = 96000;
    gs.setParams(p);
    gs.setRuntimeGain(3.0f); // por encima del cap documentado

    auto L = makeSine(8192, 0.5f, 1000.f);
    auto R = L;
    gs.processOutput(L.data(), R.data(), static_cast<int>(L.size()));

    // Tras converger (segunda mitad del bloque), la salida no debe superar
    // la entrada original — si runtimeGain amplificara por encima de 1.0,
    // esto fallaría con valores > 0.5.
    for (size_t i = L.size() / 2; i < L.size(); ++i) {
        ASSERT_LE(std::fabs(L[i]), 0.5f + 1e-3f)
            << "setRuntimeGain(3.0) amplificó por encima de unity en muestra " << i
            << " — ¿regresó el bug del cap 1.5x documentado en el header?";
    }
}

// processInput/processOutput deben ser transparentes con ganancia 1.0 tras
// converger — no deben introducir ruido ni offset por sí mismos.
TEST(GainStageReal, UnityGainIsTransparentAfterConverging) {
    GainStage gs;
    DSPParams p; p.master = 0.0f; p.sampleRate = 96000;
    gs.setParams(p);
    auto L = makeSine(4096, 0.4f, 1000.f);
    auto Lref = L;
    auto R = L;
    gs.processOutput(L.data(), R.data(), static_cast<int>(L.size()));
    for (size_t i = L.size() - 100; i < L.size(); ++i) // cola: ya convergido
        ASSERT_NEAR(L[i], Lref[i], 1e-3f) << "muestra " << i;
}

// ── StereoWidener ────────────────────────────────────────────────────────

// FIX (error de razonamiento propio, corregido antes de commitear): la
// primera versión de este test sumaba L+R a mono para detectar
// "cancelación de fase". Esto no prueba nada: en CUALQUIER widener M/S
// (protegido o no), outL+outR = 2*mid siempre, sin importar el ancho —
// el side se cancela exactamente en la suma digital por construcción
// matemática, sea cual sea bassFactor. La protección real de "mono-safety"
// que el header describe es sobre sumas ACÚSTICAS imperfectas en el mundo
// real (dos altavoces, comb filtering) — no reproducible con una suma
// digital exacta, así que esa no era la propiedad correcta a verificar.
//
// El contrato real, verificable a nivel de código (leído en StereoWidener.cpp):
// bassFactor limita el boost del SIDE de baja frecuencia a 0.25 en w=2 (vs.
// 2.0 sin protección) — "75% menos boost de side en graves al ancho máximo".
// Verificamos ESO directamente: con contenido puramente "side" a 60Hz
// (L=-R), la magnitud del side de salida a w=2 debe quedar muy por debajo
// de lo que un ensanche naive sin protección (side_in * width) produciría.
TEST(StereoWidenerReal, LowFrequencySideBoostIsLimitedAtMaxWidth) {
    StereoWidener sw;
    DSPParams p; p.sampleRate = 96000;
    sw.setParams(p);
    sw.setWidth(2.0f); // ancho máximo

    constexpr float kSideAmplitude = 0.5f;
    auto sideSignal = makeSine(8192, kSideAmplitude, 60.f); // 60Hz: bajo el corte de 150Hz
    std::vector<float> L(sideSignal.size()), R(sideSignal.size());
    for (size_t i = 0; i < sideSignal.size(); ++i) {
        L[i] =  sideSignal[i]; // L=-R => mid=0, side=sideSignal (contenido side puro)
        R[i] = -sideSignal[i];
    }
    sw.process(L.data(), R.data(), static_cast<int>(L.size()));

    // Magnitud pico del side de SALIDA en la cola (ya convergido widthNow_).
    float outSidePeak = 0.f;
    for (size_t i = L.size() - 2048; i < L.size(); ++i) {
        const float outSide = 0.5f * (L[i] - R[i]);
        outSidePeak = std::max(outSidePeak, std::fabs(outSide));
    }
    // Sin protección de graves, un widener naive a w=2 produciría
    // side_out ≈ side_in * 2 = 1.0. Exigimos quedar muy por debajo de eso
    // (bassFactor≈0.25 documentado => side_out esperado ≈ 0.125).
    const float naiveUnprotected = kSideAmplitude * 2.0f;
    EXPECT_LT(outSidePeak, naiveUnprotected * 0.5f)
        << "El boost de side en graves a ancho máximo no está limitado — "
        << "¿regresó el widener M/S puro sin crossover documentado en el header? "
        << "outSidePeak=" << outSidePeak << " (naive sin protección sería ~" << naiveUnprotected << ")";
}

// setWidth(1.0) (neutro) no debe alterar significativamente una señal
// estéreo ya correlacionada.
TEST(StereoWidenerReal, UnityWidthIsNearTransparent) {
    StereoWidener sw;
    DSPParams p; p.sampleRate = 96000;
    sw.setParams(p);
    sw.setWidth(1.0f);
    auto L = makeSine(4096, 0.5f, 1000.f);
    auto Lref = L;
    auto R = L;
    sw.process(L.data(), R.data(), static_cast<int>(L.size()));
    for (size_t i = L.size() - 100; i < L.size(); ++i)
        ASSERT_NEAR(L[i], Lref[i], 0.02f) << "muestra " << i;
}

// ── Compressor ───────────────────────────────────────────────────────────

// Comportamiento real de un compresor: una señal muy por encima del
// threshold debe salir con MENOS rango dinámico relativo que una señal
// que nunca lo cruza. Verificamos el efecto real, no una fórmula local.
TEST(CompressorReal, SignalAboveThresholdGetsGainReduced) {
    Compressor comp;
    DSPParams p;
    p.sampleRate = 96000;
    comp.setParams(p);
    comp.setThreshold(-12.0f); // mismo default documentado en el header
    comp.setRatio(8.0f);       // compresión fuerte y verificable
    comp.setAttack(2.0f);
    comp.setRelease(80.0f);

    // Señal caliente y sostenida (muy por encima de -12dB ~ 0.25 lineal)
    // para dar tiempo al envelope follower a asentarse.
    auto L = makeSine(20000, 0.9f, 200.f);
    auto R = L;
    const float peakBefore = *std::max_element(L.begin(), L.end());
    comp.process(L.data(), R.data(), static_cast<int>(L.size()));
    const float peakAfter = *std::max_element(L.begin() + 15000, L.end());

    EXPECT_LT(peakAfter, peakBefore * 0.85f)
        << "Señal muy por encima del threshold no se redujo — "
        << "peakBefore=" << peakBefore << " peakAfter(cola)=" << peakAfter;
}

// setRuntimeAmount documenta convergencia one-pole ~20ms — NO un salto
// instantáneo. Verificamos que, en el primer bloque tras cambiar el
// runtime amount, la salida no da un salto discontinuo grande entre
// muestras consecutivas (zipper), a diferencia del bug que el propio
// header describe como ya corregido.
TEST(CompressorReal, RuntimeAmountConvergesSmoothlyNoZipper) {
    Compressor comp;
    DSPParams p; p.sampleRate = 96000;
    comp.setParams(p);
    comp.setThreshold(-12.0f);
    comp.setRatio(6.0f);

    auto L = makeSine(4096, 0.8f, 300.f);
    auto R = L;
    // Primer bloque: deja asentar el envelope follower.
    comp.process(L.data(), R.data(), static_cast<int>(L.size()));

    // Cambiar el runtime amount de golpe (0 -> 1) y procesar un bloque más,
    // vigilando el salto muestra-a-muestra al inicio del nuevo bloque.
    comp.setRuntimeAmount(1.0f);
    auto L2 = makeSine(512, 0.8f, 300.f);
    auto R2 = L2;
    comp.process(L2.data(), R2.data(), static_cast<int>(L2.size()));

    float maxJump = 0.f;
    for (size_t i = 1; i < L2.size(); ++i)
        maxJump = std::max(maxJump, std::fabs(L2[i] - L2[i - 1]));
    // Salto máximo esperado para un tono de 300Hz sin discontinuidad de
    // ganancia añadida — generoso pero descarta un escalón de zipper real.
    EXPECT_LT(maxJump, 0.25f)
        << "Salto muestra-a-muestra grande tras setRuntimeAmount() — "
        << "¿regresó el zipper documentado en el header? maxJump=" << maxJump;
}
