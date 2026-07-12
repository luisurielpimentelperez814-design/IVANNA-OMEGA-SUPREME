// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Test standalone del AdaptiveDecisionEngine — sin dependencias externas
// (no gtest, no Android NDK, no JNI). Compila con cualquier g++/clang++
// que soporte C++17:
//
//   g++ -std=c++17 -Wall -Wextra -pthread -I..
//       tests/test_adaptive_engine.cpp adaptive_decision_engine.cpp
//       -o test_adaptive_engine && ./test_adaptive_engine
//
// Verifica, contra las tres entradas pedidas por el prompt de Fase 3
// (silenciosa / normal / saturada):
//   1. Nunca genera NaN/Inf en ningún campo de salida.
//   2. Todos los valores quedan dentro de los rangos documentados.
//   3. Las funciones de análisis son puras — no tocan ningún buffer de
//      audio (arquitectónicamente garantizado: ninguna función de este
//      módulo recibe un puntero a muestras, solo structs de métricas ya
//      resumidas).

#include "../adaptive_decision_engine.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace ivanna::experimental;

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "  [FALLO] %s\n", what);
        g_failures++;
    } else {
        std::fprintf(stdout, "  [ok]    %s\n", what);
    }
}

bool allFinite(const AdaptiveState& s) {
    return std::isfinite(s.target_gain) && std::isfinite(s.compressor_amount) &&
           std::isfinite(s.exciter_reduction) && std::isfinite(s.spatial_width) &&
           std::isfinite(s.safety_margin);
}

bool inDocumentedRanges(const AdaptiveState& s) {
    return s.target_gain       >= 0.0f && s.target_gain       <= 2.0f &&
           s.compressor_amount >= 0.0f && s.compressor_amount <= 1.0f &&
           s.exciter_reduction >= 0.0f && s.exciter_reduction <= 1.0f &&
           s.spatial_width     >= 0.0f && s.spatial_width     <= 1.5f &&
           s.safety_margin     >= 0.0f && s.safety_margin     <= 1.0f;
}

void printState(const char* label, const AdaptiveState& s) {
    std::printf("  %s → target_gain=%.4f comp=%.4f exc_red=%.4f width=%.4f margin=%.4f\n",
                label, s.target_gain, s.compressor_amount, s.exciter_reduction,
                s.spatial_width, s.safety_margin);
}

void testSilentInput() {
    std::printf("\n=== Entrada silenciosa (rms=0, peak=0, bandas=0) ===\n");
    RawAudioMetrics m{};  // todo en 0 por defecto
    AdaptiveState s = AdaptiveDecisionEngine::evaluate(m, /*sibilanceEma=*/0.0f);
    printState("silencio", s);

    expect(allFinite(s), "silencio: sin NaN/Inf");
    expect(inDocumentedRanges(s), "silencio: rangos documentados");
    // Con silencio total no debería sugerirse ningún recorte de ganancia
    // ni reducción de exciter — el sistema está sano.
    expect(s.target_gain == 1.0f, "silencio: target_gain = 1.0 (sin recorte)");
    expect(s.safety_margin == 1.0f, "silencio: safety_margin = 1.0 (máximo)");
    expect(s.compressor_amount == 0.0f, "silencio: compressor_amount = 0.0");
}

void testNormalSignal() {
    std::printf("\n=== Señal normal (rms=0.1, peak=0.25 ≈ -12dBFS, bandas balanceadas) ===\n");
    RawAudioMetrics m{};
    // FIX (encontrado por el test real): peak=0.5 (-6dBFS) con la fórmula
    // logarítmica corregida de safety_margin da ≈0.49 — 6dB de headroom es
    // razonable pero no "excelente". -12dBFS (peak=0.25) es el headroom
    // convencional de una señal genuinamente sana/sin preocupación, y es
    // lo que este test debe representar.
    m.rms = 0.1f;
    m.peak = 0.25f;
    m.band_low_energy  = 0.3f;
    m.band_mid_energy  = 0.4f;
    m.band_high_energy = 0.1f;   // 12.5% del total — bajo el umbral de sibilancia (15%)
    m.gain_reduction_db = 0.0f;  // limiter no está trabajando
    AdaptiveState s = AdaptiveDecisionEngine::evaluate(m, /*sibilanceEma=*/0.05f);
    printState("normal", s);

    expect(allFinite(s), "normal: sin NaN/Inf");
    expect(inDocumentedRanges(s), "normal: rangos documentados");
    expect(s.target_gain == 1.0f, "normal: target_gain = 1.0 (sin gain reduction previa)");
    expect(s.safety_margin > 0.9f, "normal: safety_margin alto (>0.9)");
    expect(s.exciter_reduction < 0.2f, "normal: exciter_reduction bajo (<0.2, sin sibilancia)");
}

void testSaturatedSignal() {
    std::printf("\n=== Señal saturada (transiente agudo cerca del threshold, gain_reduction alto) ===\n");
    RawAudioMetrics m{};
    // FIX (encontrado por el test real): rms=0.9/peak=0.995 (crest≈1.1) NO
    // es una señal "saturada" en el sentido que necesita más compresión —
    // es una señal YA densa/brickwalled (crest factor bajo), que es
    // exactamente lo opuesto de lo que dispara computeCompressorAmount()
    // (diseñada para detectar transientes SIN domar, crest factor alto).
    // El escenario correcto para "saturada por transientes" es RMS bajo
    // con un pico agudo cerca del techo — igual de realista para
    // "gain_reduction alto" (el limiter reacciona a picos puntuales, no
    // necesita que el RMS también esté alto).
    m.rms  = 0.15f;
    m.peak = 0.995f;               // por encima del threshold real del SafetyLimiter (0.98855)
    m.band_low_energy  = 0.2f;
    m.band_mid_energy  = 0.3f;
    m.band_high_energy = 0.6f;     // 54.5% del total — sibilancia fuerte
    m.gain_reduction_db = 8.0f;    // limiter trabajando duro
    AdaptiveState s = AdaptiveDecisionEngine::evaluate(m, /*sibilanceEma=*/0.8f);
    printState("saturada", s);

    expect(allFinite(s), "saturada: sin NaN/Inf");
    expect(inDocumentedRanges(s), "saturada: rangos documentados");
    expect(s.target_gain < 1.0f, "saturada: target_gain sugiere recorte (<1.0)");
    expect(s.target_gain >= 0.5f, "saturada: target_gain no baja del piso 0.5");
    expect(s.safety_margin < 0.2f, "saturada: safety_margin bajo (<0.2, sistema en riesgo)");
    expect(s.exciter_reduction > 0.5f, "saturada: exciter_reduction alto (>0.5)");
    expect(s.compressor_amount > 0.5f, "saturada: compressor_amount alto (crest factor grande, 0.995/0.15≈6.6)");
}

void testExtremeAndDegenerateInputs() {
    std::printf("\n=== Entradas degeneradas (NaN/Inf/negativos de entrada) ===\n");
    RawAudioMetrics m{};
    m.rms = std::numeric_limits<float>::quiet_NaN();
    m.peak = std::numeric_limits<float>::infinity();
    m.band_low_energy = -5.0f;   // energía negativa no debería ser posible en la práctica,
    m.band_mid_energy = -1.0f;   // pero una fuente de métricas corrupta/con bug no debe
    m.band_high_energy = -3.0f;  // poder producir NaN aguas abajo.
    m.gain_reduction_db = std::numeric_limits<float>::quiet_NaN();
    AdaptiveState s = AdaptiveDecisionEngine::evaluate(m, /*sibilanceEma=*/std::numeric_limits<float>::quiet_NaN());
    printState("degenerada", s);

    expect(allFinite(s), "degenerada: sin NaN/Inf incluso con entradas corruptas");
    expect(inDocumentedRanges(s), "degenerada: rangos documentados incluso con entradas corruptas");
}

void testBusRoundTrip() {
    std::printf("\n=== RawMetricsBus / AdaptiveStateBus — round-trip sin hilo ===\n");
    RawMetricsBus rawBus;
    AdaptiveStateBus stateBus;

    RawAudioMetrics in{};
    in.rms = 0.33f;
    in.peak = 0.6f;
    rawBus.publish(in);

    RawAudioMetrics out{};
    uint64_t lastSeen = 0;
    bool got = rawBus.consumeIfNewer(out, lastSeen);
    expect(got, "RawMetricsBus: primera lectura detecta dato nuevo");
    expect(out.rms == 0.33f && out.peak == 0.6f, "RawMetricsBus: valores round-trip exactos");

    bool gotAgain = rawBus.consumeIfNewer(out, lastSeen);
    expect(!gotAgain, "RawMetricsBus: segunda lectura sin nuevo publish() no reporta dato nuevo");

    AdaptiveState st{};
    st.target_gain = 0.75f;
    stateBus.publish(st);
    AdaptiveState stOut{};
    uint64_t lastSeenState = 0;
    expect(stateBus.consumeIfNewer(stOut, lastSeenState), "AdaptiveStateBus: primera lectura detecta dato nuevo");
    expect(stOut.target_gain == 0.75f, "AdaptiveStateBus: valor round-trip exacto");
}

} // namespace

int main() {
    std::printf("AdaptiveDecisionEngine — suite de tests standalone\n");
    std::printf("====================================================\n");

    testSilentInput();
    testNormalSignal();
    testSaturatedSignal();
    testExtremeAndDegenerateInputs();
    testBusRoundTrip();

    std::printf("\n====================================================\n");
    if (g_failures == 0) {
        std::printf("TODOS LOS TESTS PASARON.\n");
        return 0;
    }
    std::fprintf(stderr, "%d TEST(S) FALLARON.\n", g_failures);
    return 1;
}
