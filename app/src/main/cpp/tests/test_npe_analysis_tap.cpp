/**
 * test_npe_analysis_tap.cpp — el tap de análisis del NPE clasifica sin tocar el audio.
 *
 * Síntoma de campo (captura de pantalla del dispositivo): "AI ANALYSIS — · 0% confidence" y
 * "GÉNERO —" con el NPE activo. Causa: con DSPBridge nativo el servicio no alimenta al NPE
 * (a propósito, para no duplicar procesamiento) y el género solo se actualizaba al procesar.
 * Estas pruebas fijan que el tap (mismo AutonomousBrain/Synthesizer) produce género y firma
 * a partir de la entrada, es determinista respecto al tamaño de bloque y no modifica la señal.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "neuromorphic/npe_analysis_tap.hpp"

using ivanna::acoustic::AutonomousBrain;
using ivanna::acoustic::Synthesizer;
using ivanna::acoustic::npeAnalyzeStereo;

namespace {
constexpr float kSr = 48000.f;
const char* kNone = "\xe2\x80\x94";                 // "—"

// Señal tipo música comprimida: graves con ataques + medios + agudos (factor de cresta bajo).
void musicLike(std::vector<float>& l, std::vector<float>& r, int n, float amp) {
    l.resize(n); r.resize(n);
    for (int i = 0; i < n; ++i) {
        const double t = i / (double)kSr;
        const double kick = std::sin(2 * 3.14159265 * 55.0 * t) * (0.5 + 0.5 * std::sin(2 * 3.14159265 * 2.0 * t));
        const double v = 0.5 * kick + 0.25 * std::sin(2 * 3.14159265 * 1800.0 * t) + 0.15 * std::sin(2 * 3.14159265 * 7000.0 * t);
        l[i] = (float)(amp * v);
        r[i] = (float)(amp * v * 0.98);
    }
}
std::string feed(AutonomousBrain& b, Synthesizer& s, const std::vector<float>& l, const std::vector<float>& r, int block) {
    for (size_t off = 0; off + block <= l.size(); off += block)
        npeAnalyzeStereo(b, s, l.data() + off, r.data() + off, block, kSr);
    return b.getLastGenre();
}
} // namespace

TEST(NpeAnalysisTap, StartsUnclassifiedAndClassifiesAfterOneWindow) {
    AutonomousBrain brain; Synthesizer synth;
    EXPECT_STREQ(brain.getLastGenre(), kNone);       // estado inicial: sin clasificar
    std::vector<float> l, r; musicLike(l, r, 4096 * 3, 0.6f);
    const std::string g = feed(brain, synth, l, r, 480);
    EXPECT_STRNE(g.c_str(), kNone) << "el tap no produjo género tras varias ventanas";
}

TEST(NpeAnalysisTap, ResultDoesNotDependOnBlockSize) {
    // 30720 = mcm(64, 480, 512, 1024, 2048): todos los tamaños de bloque alimentan EXACTAMENTE las
    // mismas muestras (con otra longitud se evaluarían ventanas distintas y la comparación no valdría).
    std::vector<float> l, r; musicLike(l, r, 30720, 0.6f);
    std::string ref;
    for (int blk : {64, 480, 512, 1024, 2048}) {
        AutonomousBrain brain; Synthesizer synth;
        const std::string g = feed(brain, synth, l, r, blk);
        if (ref.empty()) ref = g; else EXPECT_EQ(ref, g) << "bloque " << blk;
        float c[7]; synth.classify(c);
        for (float v : c) EXPECT_TRUE(std::isfinite(v));
    }
}

TEST(NpeAnalysisTap, NeverModifiesTheInputSignal) {
    AutonomousBrain brain; Synthesizer synth;
    std::vector<float> l, r; musicLike(l, r, 4096, 0.6f);
    const auto l0 = l, r0 = r;
    npeAnalyzeStereo(brain, synth, l.data(), r.data(), (int)l.size(), kSr);
    EXPECT_EQ(0, std::memcmp(l.data(), l0.data(), l.size() * sizeof(float)));
    EXPECT_EQ(0, std::memcmp(r.data(), r0.data(), r.size() * sizeof(float)));
}

TEST(NpeAnalysisTap, SilenceAndInvalidArgumentsAreSafeAndKeepTheLastGenre) {
    AutonomousBrain brain; Synthesizer synth;
    std::vector<float> l, r; musicLike(l, r, 4096 * 3, 0.6f);
    const std::string g = feed(brain, synth, l, r, 480);
    std::vector<float> z(4096 * 2, 0.f);
    feed(brain, synth, z, z, 480);                    // silencio: no pisa el género
    EXPECT_EQ(g, std::string(brain.getLastGenre()));
    npeAnalyzeStereo(brain, synth, nullptr, nullptr, 480, kSr);   // no revienta
    npeAnalyzeStereo(brain, synth, z.data(), z.data(), 0, kSr);
    SUCCEED();
}
