// ============================================================================
// IVANNA — Test Suite: Acoustic Unity Engine, lazo cerrado MEDIDO (v2.5.15)
//
// El motor deja de decidir solo por reglas y mide el RESULTADO (original vs salida
// final). Cada test fija un conflicto real entre actuadores sobre audio sintético:
//   EXP (expansión lateral), PHASE (correlación), CLAR (cresta), FATIGUE (agudos),
// la recuperación cuando el conflicto desaparece, la transparencia cuando no hay
// conflicto, el interruptor, el arranque en frío y la robustez ante no finitos.
// Es medición en host con señales sintéticas, NO escucha en dispositivo.
// ============================================================================
#include <gtest/gtest.h>
#include "acoustic_unity_engine.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

using namespace ivanna::unity;

namespace {
constexpr int   kBlock = 320;
constexpr float kSr    = 48000.0f;

struct Rng {
    uint32_t s{0x1234ABCDu};
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float>((s >> 8) & 0xFFFF) / 32768.0f - 1.0f; }
    // Aproximación gaussiana (suma de 4 uniformes): cresta realista (~11-12 dB en 1 s).
    float gauss() { return 0.5f * (next() + next() + next() + next()) * 0.866f; }
};

struct Sig { std::vector<float> L, R; };

// Estéreo con correlación objetivo c (mezcla de dos ruidos gaussianos), RMS ~ amp.
Sig makeStereo(int n, float c, float amp, uint32_t seed) {
    Rng r; r.s ^= seed;
    Sig s; s.L.resize(n); s.R.resize(n);
    const float a = c, b = std::sqrt(std::max(0.f, 1.f - c * c));
    for (int i = 0; i < n; ++i) {
        const float x = r.gauss(), y = r.gauss();
        s.L[i] = amp * x;
        s.R[i] = amp * (a * x + b * y);
    }
    return s;
}

// Alimenta `seconds` de audio bloque a bloque. `shape(dry, wet)` fabrica la "salida" del DSP.
template <class Shape>
void run(ClosedLoopCoherenceMeter& m, const Sig& dry, float seconds, Shape shape) {
    const int total = static_cast<int>(seconds * kSr);
    std::vector<float> wl(kBlock), wr(kBlock);
    for (int off = 0; off + kBlock <= total; off += kBlock) {
        const int base = off % static_cast<int>(dry.L.size() - kBlock);
        shape(&dry.L[base], &dry.R[base], wl.data(), wr.data(), kBlock);
        m.observe(&dry.L[base], &dry.R[base], wl.data(), wr.data(), kBlock, true);
    }
}

void identityShape(const float* dl, const float* dr, float* wl, float* wr, int n) {
    for (int i = 0; i < n; ++i) { wl[i] = dl[i]; wr[i] = dr[i]; }
}
} // namespace

TEST(UnityClosedLoop, TransparentChainKeepsFullBudgets) {
    ClosedLoopCoherenceMeter m; m.prepare(kSr);
    const Sig dry = makeStereo(kBlock * 600, 0.7f, 0.2f, 1u);
    run(m, dry, 6.0f, identityShape);
    ASSERT_FLOAT_EQ(m.confidence(), 1.0f);
    EXPECT_FLOAT_EQ(m.budget().spatial, 1.0f);
    EXPECT_FLOAT_EQ(m.budget().room, 1.0f);
    EXPECT_FLOAT_EQ(m.budget().exciter, 1.0f);
    EXPECT_FLOAT_EQ(m.budget().dynamics, 1.0f);
    for (int a = 0; a < 4; ++a) EXPECT_LT(m.excessDb(a), 0.0f) << "eje " << a;
}

TEST(UnityClosedLoop, OverWideningCutsSpatialAndRoomThenRecovers) {
    ClosedLoopCoherenceMeter m; m.prepare(kSr);
    const Sig dry = makeStereo(kBlock * 600, 0.8f, 0.2f, 2u);
    // Salida con el canal lateral x6 (+15.6 dB): lateralidad que el original no justifica.
    auto widen = [](const float* dl, const float* dr, float* wl, float* wr, int n) {
        for (int i = 0; i < n; ++i) {
            const float mid = 0.5f * (dl[i] + dr[i]), side = 0.5f * (dl[i] - dr[i]) * 6.0f;
            wl[i] = mid + side; wr[i] = mid - side;
        }
    };
    run(m, dry, 5.0f, widen);
    EXPECT_GT(m.excessDb(0), 3.0f) << "EXP debe detectar la sobre-expansión";
    EXPECT_LT(m.budget().spatial, 0.75f);
    EXPECT_LT(m.budget().room, 0.60f);
    EXPECT_GE(m.budget().room, ClosedLoopCoherenceMeter::kMinRoom);          // piso de fidelidad
    EXPECT_GE(m.budget().spatial, ClosedLoopCoherenceMeter::kMinSpatial);

    run(m, dry, 14.0f, identityShape);                                       // el conflicto desaparece
    EXPECT_GT(m.budget().spatial, 0.99f);
    EXPECT_GT(m.budget().room, 0.99f);
}

TEST(UnityClosedLoop, PhaseDestructionIsDetectedAndCutsSpatial) {
    ClosedLoopCoherenceMeter m; m.prepare(kSr);
    const Sig dry = makeStereo(kBlock * 600, 0.95f, 0.2f, 3u);
    auto invert = [](const float* dl, const float* dr, float* wl, float* wr, int n) {
        for (int i = 0; i < n; ++i) { wl[i] = dl[i]; wr[i] = -dr[i]; }       // R en antifase
    };
    run(m, dry, 4.0f, invert);
    EXPECT_LT(m.corrWet(), -0.8f);
    EXPECT_GT(m.corrDry(), 0.85f);
    EXPECT_GT(m.excessDb(1), 10.0f);
    EXPECT_LT(m.budget().spatial, 0.45f);
    EXPECT_LT(m.budget().room, 0.45f);
}

TEST(UnityClosedLoop, CrestLossCutsDynamicsAndRoom) {
    ClosedLoopCoherenceMeter m; m.prepare(kSr);
    const Sig dry = makeStereo(kBlock * 600, 0.6f, 0.2f, 4u);
    // "Limitador/compresor" que aplasta a ±1 sigma (sigma real = 0.5*amp = 0.1): la cresta cae ~8 dB.
    auto crush = [](const float* dl, const float* dr, float* wl, float* wr, int n) {
        constexpr float lim = 0.1f;
        for (int i = 0; i < n; ++i) {
            wl[i] = std::fmax(-lim, std::fmin(lim, dl[i]));
            wr[i] = std::fmax(-lim, std::fmin(lim, dr[i]));
        }
    };
    run(m, dry, 5.0f, crush);
    EXPECT_GT(m.crestLossDb(), 4.0f);
    EXPECT_GT(m.excessDb(2), 0.0f);
    EXPECT_LT(m.budget().dynamics, 0.70f);
    EXPECT_LT(m.budget().room, 0.90f);
}

TEST(UnityClosedLoop, HighFrequencyExcessCutsExciter) {
    ClosedLoopCoherenceMeter m; m.prepare(kSr);
    // Original grave (LP) con muy poco agudo; la salida le suma agudo (diferencia de ruido = pasa-altos).
    Sig dry; dry.L.resize(kBlock * 600); dry.R.resize(kBlock * 600);
    Sig hf = dry;
    Rng r; float lpL = 0, lpR = 0, pL = 0, pR = 0;
    for (size_t i = 0; i < dry.L.size(); ++i) {
        const float wl = r.gauss(), wr = r.gauss();
        lpL += 0.03f * (wl - lpL); lpR += 0.03f * (wr - lpR);
        dry.L[i] = 0.8f * lpL; dry.R[i] = 0.8f * lpR;
        hf.L[i] = 0.5f * (wl - pL); hf.R[i] = 0.5f * (wr - pR);
        pL = wl; pR = wr;
    }
    std::vector<float> wl(kBlock), wr(kBlock);
    for (int off = 0; off + kBlock <= 5 * 48000; off += kBlock) {
        const int base = off % static_cast<int>(dry.L.size() - kBlock);
        for (int i = 0; i < kBlock; ++i) { wl[i] = dry.L[base + i] + hf.L[base + i]; wr[i] = dry.R[base + i] + hf.R[base + i]; }
        m.observe(&dry.L[base], &dry.R[base], wl.data(), wr.data(), kBlock, true);
    }
    EXPECT_GT(m.hfExcessDb(), 6.0f);
    EXPECT_GT(m.excessDb(3), 3.0f);
    EXPECT_LT(m.budget().exciter, 0.55f);
    EXPECT_FLOAT_EQ(m.budget().dynamics, 1.0f);                              // no toca lo que no compite
}

TEST(UnityClosedLoop, VoiceToleratesLessColoration) {
    const Sig dry = makeStereo(kBlock * 600, 0.8f, 0.2f, 5u);
    auto widen = [](const float* dl, const float* dr, float* wl, float* wr, int n) {
        for (int i = 0; i < n; ++i) {
            const float mid = 0.5f * (dl[i] + dr[i]), side = 0.5f * (dl[i] - dr[i]) * 3.0f;
            wl[i] = mid + side; wr[i] = mid - side;
        }
    };
    ClosedLoopCoherenceMeter music; music.prepare(kSr); music.setVoice(0.0f);
    ClosedLoopCoherenceMeter voice; voice.prepare(kSr); voice.setVoice(1.0f);
    run(music, dry, 3.0f, widen);
    run(voice, dry, 3.0f, widen);
    EXPECT_GT(voice.excessDb(0), music.excessDb(0));                         // la voz permite menos expansión
    EXPECT_LE(voice.budget().room, music.budget().room);
}

TEST(UnityClosedLoop, NonFiniteBlockDoesNotPoisonState) {
    ClosedLoopCoherenceMeter m; m.prepare(kSr);
    const Sig dry = makeStereo(kBlock * 600, 0.7f, 0.2f, 6u);
    run(m, dry, 2.0f, identityShape);
    std::vector<float> wl(kBlock, 0.1f), wr(kBlock, 0.1f);
    wl[7] = std::numeric_limits<float>::quiet_NaN();
    wr[9] = std::numeric_limits<float>::infinity();
    m.observe(&dry.L[0], &dry.R[0], wl.data(), wr.data(), kBlock, true);
    run(m, dry, 1.0f, identityShape);
    float t[ClosedLoopCoherenceMeter::kTelemetrySize];
    m.telemetry(t);
    for (float v : t) EXPECT_TRUE(std::isfinite(v));
    EXPECT_FLOAT_EQ(m.budget().room, 1.0f);
}

TEST(UnityClosedLoop, SilenceRelaxesInsteadOfDeciding) {
    ClosedLoopCoherenceMeter m; m.prepare(kSr);
    const Sig dry = makeStereo(kBlock * 600, 0.95f, 0.2f, 7u);
    auto invert = [](const float* dl, const float* dr, float* wl, float* wr, int n) {
        for (int i = 0; i < n; ++i) { wl[i] = dl[i]; wr[i] = -dr[i]; }
    };
    run(m, dry, 3.0f, invert);
    const float cut = m.budget().room;
    ASSERT_LT(cut, 0.6f);
    std::vector<float> z(kBlock, 0.0f);
    for (int i = 0; i < 300; ++i) m.observe(z.data(), z.data(), z.data(), z.data(), kBlock, true);
    EXPECT_GT(m.budget().room, cut);                                          // sin información: relaja, no decide
}

// ── Integración en el motor ────────────────────────────────────────────────
TEST(UnityEngineIntegration, MeasuredBudgetsReachContextAndReviveDeadFields) {
    auto& e = AcousticUnityEngine::instance();
    e.reset(); e.setSampleRate(kSr); e.setEnabled(true);
    const Sig dry = makeStereo(kBlock * 600, 0.95f, 0.2f, 8u);
    std::vector<float> wl(kBlock), wr(kBlock);
    for (int off = 0; off < 5 * 48000; off += kBlock) {
        const int base = off % static_cast<int>(dry.L.size() - kBlock);
        e.coordinateAcousticOrganism(&dry.L[base], &dry.R[base], kBlock, 0.0f, 0.1f, false, false, true, 0.3f);
        for (int i = 0; i < kBlock; ++i) { wl[i] = dry.L[base + i]; wr[i] = -dry.R[base + i]; }
        e.observeOutcome(&dry.L[base], &dry.R[base], wl.data(), wr.data(), kBlock);
    }
    const auto c = e.getContext();
    EXPECT_FLOAT_EQ(c.closedLoopConfidence, 1.0f);
    EXPECT_LT(c.roomReverbBudget, 0.6f);       // antes: campo siempre 1.0 y sin consumidor
    EXPECT_LT(c.spatialBudget, 0.6f);
    EXPECT_GT(c.spectralCrestDb, 3.0f);        // antes: constante 12.0 sin escritor
    EXPECT_GE(c.sibilanceEnergyRatio, 0.0f);
    EXPECT_LE(c.sibilanceEnergyRatio, 1.0f);
    EXPECT_TRUE(std::isfinite(c.dynamicHeadroomDb));
    float t[ClosedLoopCoherenceMeter::kTelemetrySize];
    e.loopTelemetry(t);
    EXPECT_LT(t[1], -0.8f);                    // corrWet
    EXPECT_LT(t[12], 0.6f);                    // presupuesto room publicado
}

TEST(UnityEngineIntegration, DisabledPublishesNeutralContext) {
    auto& e = AcousticUnityEngine::instance();
    e.reset(); e.setSampleRate(kSr); e.setEnabled(true);
    const Sig dry = makeStereo(kBlock * 600, 0.95f, 0.2f, 9u);
    std::vector<float> wl(kBlock), wr(kBlock);
    for (int off = 0; off < 4 * 48000; off += kBlock) {
        const int base = off % static_cast<int>(dry.L.size() - kBlock);
        e.coordinateAcousticOrganism(&dry.L[base], &dry.R[base], kBlock, 0.9f, 0.1f, false, false, true, 0.3f);
        for (int i = 0; i < kBlock; ++i) { wl[i] = dry.L[base + i]; wr[i] = -dry.R[base + i]; }
        e.observeOutcome(&dry.L[base], &dry.R[base], wl.data(), wr.data(), kBlock);
    }
    ASSERT_LT(e.getContext().roomReverbBudget, 0.9f);
    e.setEnabled(false);
    e.coordinateAcousticOrganism(&dry.L[0], &dry.R[0], kBlock, 0.9f, 0.1f, false, false, true, 0.3f);
    const auto c = e.getContext();
    EXPECT_FALSE(e.isEnabled());
    EXPECT_FLOAT_EQ(c.roomReverbBudget, 1.0f);
    EXPECT_FLOAT_EQ(c.spatialBudget, 1.0f);
    EXPECT_FLOAT_EQ(c.harmonicBudget, 1.0f);
    EXPECT_FLOAT_EQ(c.dynamicsBudget, 1.0f);
    EXPECT_FLOAT_EQ(c.msWidenerMultiplier, 1.0f);
    EXPECT_FLOAT_EQ(c.rirWetDuckFactor, 1.0f);
    EXPECT_EQ(c.scene, DominantAcousticScene::Neutral);
    e.setEnabled(true);
}

TEST(UnityEngineIntegration, WidenerAsksOnlyForNarrowSources) {
    auto& e = AcousticUnityEngine::instance();
    e.reset(); e.setSampleRate(kSr); e.setEnabled(true);
    const Sig narrow = makeStereo(kBlock * 4, 0.97f, 0.2f, 10u);
    const Sig wide   = makeStereo(kBlock * 4, 0.10f, 0.2f, 11u);
    e.coordinateAcousticOrganism(narrow.L.data(), narrow.R.data(), kBlock, 0.0f, 0.0f, false, false, false, 0.0f);
    const float msNarrow = e.getContext().msWidenerMultiplier;
    e.coordinateAcousticOrganism(wide.L.data(), wide.R.data(), kBlock, 0.0f, 0.0f, false, false, false, 0.0f);
    const float msWide = e.getContext().msWidenerMultiplier;
    EXPECT_GT(msNarrow, 1.15f);                // casi mono: hay margen real de ensanche
    EXPECT_FLOAT_EQ(msWide, 1.0f);             // ya ancha: no se pide más lateralidad (antes pedía +30 %)
}

TEST(UnityEngineIntegration, ColdStartRampIsSmoothMonotonicAndRearms) {
    auto& e = AcousticUnityEngine::instance();
    e.reset(); e.setSampleRate(kSr); e.setEnabled(true);
    std::vector<float> in(kBlock, 0.2f), l, r;
    std::vector<float> gain;
    auto feed = [&](bool audible) {
        l.assign(kBlock, audible ? 0.2f : 0.0f); r = l;
        e.coordinateAcousticOrganism(l.data(), r.data(), kBlock, 0.0f, 0.0f, false, false, false, 0.0f);
        e.applyColdStart(l.data(), r.data(), kBlock);
        for (float v : l) gain.push_back(audible ? v / 0.2f : 1.0f);
    };
    for (int b = 0; b < 14; ++b) feed(true);
    EXPECT_LT(gain.front(), 0.01f);                                          // arranca desde silencio
    for (size_t i = 1; i < gain.size(); ++i) {
        EXPECT_GE(gain[i] + 1e-6f, gain[i - 1]) << "muestra " << i;          // monótona
        EXPECT_LT(gain[i] - gain[i - 1], 0.002f) << "muestra " << i;         // sin saltos (zero-pop)
    }
    EXPECT_NEAR(gain.back(), 1.0f, 1e-6f);                                   // identidad tras el calentamiento
    EXPECT_NEAR(gain[kBlock * 10], 1.0f, 1e-6f);

    gain.clear();
    for (int b = 0; b < 40; ++b) feed(false);                                // > 250 ms de silencio: se rearma
    gain.clear();
    feed(true);
    EXPECT_LT(gain.front(), 0.01f);
}

TEST(UnityEngineIntegration, TwoWritersNeverRaceOrCrash) {
    auto& e = AcousticUnityEngine::instance();
    e.reset(); e.setSampleRate(kSr); e.setEnabled(true);
    const Sig a = makeStereo(kBlock * 8, 0.7f, 0.2f, 12u);
    auto worker = [&]() {
        for (int i = 0; i < 3000; ++i) {
            e.coordinateAcousticOrganism(a.L.data(), a.R.data(), kBlock, 0.2f, 0.2f, false, false, true, 0.3f);
            e.observeOutcome(a.L.data(), a.R.data(), a.L.data(), a.R.data(), kBlock);
        }
    };
    std::thread t1(worker), t2(worker);
    t1.join(); t2.join();
    const auto c = e.getContext();
    EXPECT_TRUE(std::isfinite(c.spatialBudget) && c.spatialBudget >= 0.f && c.spatialBudget <= 1.f);
    EXPECT_TRUE(std::isfinite(c.roomReverbBudget) && c.roomReverbBudget >= 0.f && c.roomReverbBudget <= 1.f);
}
