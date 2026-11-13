#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "fatigue_highcut.hpp"

// Acoustic Fatigue Mitigator — high-cut IIR 1er orden (v2.5.428).
// El cutoff vive en Ruta A (proceso de la app), no en el daemon (muerto).
// alpha = 1 - exp(-2*pi*fc/sr); fc >= 20000 Hz = bypass (transparente).

namespace {

float sineRmsAfterSettle(ivanna::FatigueHighCut& f, float freqHz, float sr, int total) {
    // Llena con un seno continuo; mide el RMS de la segunda mitad (ya asentado).
    std::vector<float> l(total), r(total);
    const float phaseStep = 2.0f * 3.14159265358979323846f * freqHz / sr;
    for (int i = 0; i < total; ++i) {
        float s = std::sin(phaseStep * static_cast<float>(i));
        l[i] = s; r[i] = s;
    }
    f.process(l.data(), r.data(), total);
    double acc = 0.0;
    for (int i = total / 2; i < total; ++i) acc += static_cast<double>(l[i]) * l[i];
    return static_cast<float>(std::sqrt(acc / (total - total / 2)));
}

}  // namespace

TEST(FatigueHighCut, CoefficientMatchesRcFormula) {
    ivanna::FatigueHighCut f;
    f.setCutoff(19500.0f, 48000.0f);
    EXPECT_NEAR(f.alpha(), 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 19500.0f / 48000.0f), 1e-6f);
    f.setCutoff(16000.0f, 48000.0f);
    EXPECT_NEAR(f.alpha(), 1.0f - std::exp(-2.0f * 3.14159265358979323846f * 16000.0f / 48000.0f), 1e-6f);
    // alpha crece con el cutoff (menos atenuación arriba).
    ivanna::FatigueHighCut hi, lo;
    hi.setCutoff(19500.0f, 48000.0f);
    lo.setCutoff(16000.0f, 48000.0f);
    EXPECT_GT(hi.alpha(), lo.alpha());
}

TEST(FatigueHighCut, BypassAtOrAbove20kHzLeavesBufferUntouched) {
    ivanna::FatigueHighCut f;
    f.setCutoff(20000.0f, 48000.0f);
    EXPECT_TRUE(f.bypassed());
    std::vector<float> l = {0.5f, -0.3f, 0.9f, -0.9f};
    std::vector<float> r = l;
    f.process(l.data(), r.data(), 4);
    EXPECT_FLOAT_EQ(l[0], 0.5f);
    EXPECT_FLOAT_EQ(l[2], 0.9f);
    EXPECT_FLOAT_EQ(r[1], -0.3f);
    // El default (19500) NO es bypass, pero es prácticamente transparente.
    ivanna::FatigueHighCut d;
    EXPECT_FALSE(d.bypassed());
    EXPECT_FLOAT_EQ(d.cutoffHz(), 19500.0f);
}

TEST(FatigueHighCut, AttenuatesTrebleAndPassesBass) {
    // fc = 16000 Hz (fatiga al máximo, 0.5). Ganancia discreta 1er orden
    // (alpha = 1 - exp(-2*pi*fc/sr), polo exacto): 1 kHz -> ~0.998,
    // 18 kHz -> ~0.804 (cerca de Nyquist la respuesta discreta difiere de
    // la continua ideal 0.664; lo que importa es la atenuación RELATIVA
    // medida, no la fórmula continua). El seno de entrada tiene RMS 0.707.
    const float sr = 48000.0f;
    ivanna::FatigueHighCut f;
    f.setCutoff(16000.0f, sr);
    float bass = sineRmsAfterSettle(f, 1000.0f, sr, 4800);
    f.reset();
    f.setCutoff(16000.0f, sr);
    float treble = sineRmsAfterSettle(f, 18000.0f, sr, 4800);
    EXPECT_GT(bass, 0.65f);
    EXPECT_LT(treble, 0.60f);
    EXPECT_LT(treble, bass * 0.85f);
    EXPECT_FALSE(std::isnan(bass) || std::isnan(treble));
}

TEST(FatigueHighCut, CutoffChangeIsClickFreeAndSanitized) {
    ivanna::FatigueHighCut f;
    // Señal continua; cambiar el cutoff a mitad no debe producir NaN/Inf
    // ni un salto mayor que la propia señal.
    std::vector<float> l(512, 0.5f), r(512, 0.5f);
    f.setCutoff(19500.0f, 48000.0f);
    f.process(l.data(), r.data(), 256);
    f.setCutoff(16000.0f, 48000.0f);
    f.process(l.data() + 256, r.data() + 256, 256);
    for (float v : l) {
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_LE(std::fabs(v), 0.51f);
    }
    // Saneamiento: no finito -> default; fuera de rango -> clamp.
    f.setCutoff(std::nan(""), 48000.0f);
    EXPECT_FLOAT_EQ(f.cutoffHz(), 19500.0f);
    f.setCutoff(100.0f, 48000.0f);
    EXPECT_FLOAT_EQ(f.cutoffHz(), 1000.0f);
    f.setCutoff(99999.0f, 48000.0f);
    EXPECT_FLOAT_EQ(f.cutoffHz(), 24000.0f);
}
