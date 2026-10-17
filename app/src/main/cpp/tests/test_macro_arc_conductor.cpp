// test_macro_arc_conductor.cpp — Puerta host del arco narrativo (escala de segundos).
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Dos niveles:
//  A) MacroArcConductor aislado, con características cuyos valores se MIDIERON en el
//     MusicFeatureExtractor real (bombo+bajo vs hueco de solo colchón; ver el comentario
//     del módulo): subida pronosticada, release lento, hueco->impacto de UN solo tick
//     (también cuando el nivel general NO baja), sin falsos drops en material sin sub,
//     identidad exacta apagado / profundidad 0, límites de velocidad y de rango.
//  B) FourDExcitationChain REAL + extractor REAL con audio sintético (bombo, bajo,
//     colchón y un hueco de 1.2 s): apagado = bit-exacto frente a la cadena sin arco; el
//     arco detecta el drop con las características reales y empuja el sub en el retorno.
#include <gtest/gtest.h>

#include "music_intelligence/FourDExcitationChain.hpp"
#include "music_intelligence/MacroArcConductor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace ivanna::ime;

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kBlockMs = 5.0f;   // 10 bloques = 1 tick de 50 ms exactos

// Valores MEDIDOS en el extractor real (tema de bombo+bajo con colchón y hats):
MusicFeatures playing() {   // 3–8 s y retorno
    MusicFeatures f{};
    f.rms = 0.193f; f.bassRatio = 0.905f; f.transientRate = 0.070f; f.density = 0.949f;
    f.subBandRatio = 0.591f; f.bodyBandRatio = 0.271f; f.presenceRatio = 0.06f; f.airRatio = 0.03f;
    return f;
}
MusicFeatures gap() {       // hueco: solo colchón (el sub colapsa, transientRate sube)
    MusicFeatures f{};
    f.rms = 0.083f; f.bassRatio = 0.529f; f.transientRate = 0.156f; f.density = 0.888f;
    f.subBandRatio = 0.042f; f.bodyBandRatio = 0.347f; f.presenceRatio = 0.06f; f.airRatio = 0.03f;
    return f;
}
MusicFeatures gapKeepingLevel() {   // masterización aplastada: el nivel NO baja, solo el sub
    MusicFeatures f = gap();
    f.rms = 0.193f;
    return f;
}
MusicFeatures scaled(float k) {
    MusicFeatures f = playing();
    f.rms *= k; f.bassRatio *= k; f.transientRate *= k; f.density *= k;
    f.subBandRatio *= k; f.bodyBandRatio *= k; f.presenceRatio *= k; f.airRatio *= k;
    return f;
}

struct Seen {
    int  impactEdges = 0;
    float maxTension = 0.0f, maxAnticipation = 0.0f, maxOpenness = 0.0f, maxImpact = 0.0f;
    bool sawBuild = false;
    MacroArcOutput last{};
};

// Alimenta `seconds` de la misma característica en bloques de 5 ms.
Seen feed(MacroArcConductor& a, const MusicFeatures& f, float seconds, bool* prevPulse) {
    Seen s;
    const int blocks = static_cast<int>(seconds * 1000.0f / kBlockMs);
    for (int i = 0; i < blocks; ++i) {
        a.advance(f, kBlockMs);
        const MacroArcOutput o = a.output();
        if (o.impactPulse && !*prevPulse) ++s.impactEdges;
        *prevPulse = o.impactPulse;
        s.maxTension = std::max(s.maxTension, o.tension);
        s.maxAnticipation = std::max(s.maxAnticipation, o.anticipation);
        s.maxOpenness = std::max(s.maxOpenness, o.openness);
        s.maxImpact = std::max(s.maxImpact, o.impact);
        if (o.phase == ArcPhase::Build) s.sawBuild = true;
        s.last = o;
    }
    return s;
}

void setup(MacroArcConductor& a, bool on = true, float depth = 1.0f) {
    a.prepare(48000.0f, 240);
    a.setEnabled(on);
    a.setDepth(depth);
}

SceneApply baseScene() {
    SceneApply b{};
    b.t.wfsSpread = 0.50f; b.t.hrtfDepth = 0.50f; b.t.eqTiltDb = -1.0f; b.t.dynamicsAmount = 0.8f;
    b.t.envDepth = 0.2f; b.t.warmth = 0.6f; b.t.subPunch = 0.10f; b.t.vocalIntimacy = 0.3f;
    b.t.airHolography = 0.05f; b.t.stageElevation = 0.10f; b.t.targetIacc = 0.45f;
    b.gate = 0.9f; b.conf = 0.8f; b.styleIndex = 3;
    return b;
}

bool sameTargets(const SceneTargets& a, const SceneTargets& b) {
    return a.wfsSpread == b.wfsSpread && a.hrtfDepth == b.hrtfDepth && a.eqTiltDb == b.eqTiltDb
        && a.dynamicsAmount == b.dynamicsAmount && a.envDepth == b.envDepth && a.warmth == b.warmth
        && a.subPunch == b.subPunch && a.vocalIntimacy == b.vocalIntimacy
        && a.airHolography == b.airHolography && a.stageElevation == b.stageElevation
        && a.targetIacc == b.targetIacc;
}

} // namespace

// ═══ A) MacroArcConductor aislado ═══════════════════════════════════════════

TEST(MacroArcConductor, DisabledIsSilentAndIdentity) {
    MacroArcConductor a;            // por defecto: apagado y profundidad 0
    a.prepare(48000.0f, 240);
    bool pp = false;
    const Seen s = feed(a, playing(), 5.0f, &pp);
    EXPECT_EQ(s.last.arousal, 0.0f);
    EXPECT_EQ(s.last.openness, 0.0f);
    EXPECT_EQ(s.impactEdges, 0);
    const SceneApply base = baseScene();
    EXPECT_TRUE(sameTargets(a.modulate(base).t, base.t));
    EXPECT_EQ(a.hapticPunchBoost(), 0.0f);
}

TEST(MacroArcConductor, DepthZeroIsIdentityEvenWhenEnabled) {
    MacroArcConductor a; setup(a, true, 0.0f);
    bool pp = false;
    feed(a, playing(), 8.0f, &pp);
    feed(a, gap(), 1.0f, &pp);
    feed(a, playing(), 1.0f, &pp);
    const SceneApply base = baseScene();
    for (int i = 0; i < 400; ++i) EXPECT_TRUE(sameTargets(a.modulate(base).t, base.t));
}

TEST(MacroArcConductor, SilenceStaysCalm) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    const Seen s = feed(a, MusicFeatures{}, 20.0f, &pp);
    EXPECT_EQ(s.last.phase, ArcPhase::Calm);
    EXPECT_LT(s.last.openness, 0.01f);
    EXPECT_EQ(s.impactEdges, 0);
}

TEST(MacroArcConductor, BuildIsForecastAndReachesPeak) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    Seen total;
    float prev = 0.0f;
    const int steps = 100;              // rampa de 10 s en pasos de 0.1 s
    for (int i = 0; i < steps; ++i) {
        const Seen s = feed(a, scaled(static_cast<float>(i + 1) / steps), 0.1f, &pp);
        total.sawBuild |= s.sawBuild;
        total.maxAnticipation = std::max(total.maxAnticipation, s.maxAnticipation);
        EXPECT_GE(s.last.openness + 0.01f, prev) << "la apertura no debe cerrarse durante una subida";
        prev = s.last.openness;
    }
    EXPECT_TRUE(total.sawBuild);
    EXPECT_GT(total.maxAnticipation, 0.2f) << "debe pronosticar la subida";
    const Seen hold = feed(a, scaled(1.0f), 25.0f, &pp);
    EXPECT_EQ(hold.last.phase, ArcPhase::Peak);
    EXPECT_GT(hold.last.openness, 0.7f);
}

TEST(MacroArcConductor, ReleaseIsSlow) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    const Seen up = feed(a, playing(), 40.0f, &pp);
    const float peak = up.last.arousal;
    ASSERT_GT(peak, 0.8f);
    const Seen s1 = feed(a, MusicFeatures{}, 1.0f, &pp);
    EXPECT_GT(s1.last.arousal, 0.6f * peak) << "a 1 s sigue alta";
    const Seen s12 = feed(a, MusicFeatures{}, 11.0f, &pp);
    EXPECT_LT(s12.last.arousal, 0.2f * peak) << "a 12 s casi apagada";
}

TEST(MacroArcConductor, DropGapRaisesTensionThenExactlyOneImpact) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    feed(a, playing(), 12.0f, &pp);
    const Seen g = feed(a, gap(), 0.8f, &pp);
    EXPECT_GT(g.maxTension, 0.6f);
    EXPECT_EQ(g.impactEdges, 0) << "no hay impacto mientras el hueco continúa";
    const Seen r = feed(a, playing(), 3.0f, &pp);
    EXPECT_EQ(r.impactEdges, 1) << "el pulso de impacto debe ser de un solo tick";
    EXPECT_GT(r.maxImpact, 0.95f);
    EXPECT_LT(r.last.impact, 0.05f) << "la envolvente de impacto decae en ~1 s";
}

TEST(MacroArcConductor, DropIsDetectedEvenWhenOverallLevelDoesNotDrop) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    feed(a, playing(), 12.0f, &pp);
    const Seen g = feed(a, gapKeepingLevel(), 0.8f, &pp);
    EXPECT_GT(g.maxTension, 0.6f) << "el hueco se ve en el sub aunque rms no baje";
    const Seen r = feed(a, playing(), 3.0f, &pp);
    EXPECT_EQ(r.impactEdges, 1);
}

TEST(MacroArcConductor, LongBreakdownStillEndsInOneImpact) {
    // Un breakdown de 4 y de 12 s (la referencia del pulso no puede decaer durante el hueco).
    for (const float gapSeconds : {4.0f, 12.0f}) {
        MacroArcConductor a; setup(a);
        bool pp = false;
        feed(a, playing(), 12.0f, &pp);
        const Seen g = feed(a, gap(), gapSeconds, &pp);
        EXPECT_GT(g.maxTension, 0.6f) << gapSeconds << " s";
        EXPECT_EQ(g.impactEdges, 0) << gapSeconds << " s";
        const Seen r = feed(a, playing(), 3.0f, &pp);
        EXPECT_EQ(r.impactEdges, 1) << "tras un hueco de " << gapSeconds << " s";
    }
}

TEST(MacroArcConductor, ShortDipDoesNotTriggerImpact) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    feed(a, playing(), 12.0f, &pp);
    feed(a, gap(), 0.15f, &pp);               // 3 ticks: menos que los 5 exigidos
    const Seen r = feed(a, playing(), 3.0f, &pp);
    EXPECT_EQ(r.impactEdges, 0);
}

TEST(MacroArcConductor, MaterialWithoutSubNeverFiresDrops) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    uint32_t s = 42u;
    int edges = 0;
    float maxTension = 0.0f;
    for (int i = 0; i < 20; ++i) {            // 20 s de material tipo acústico, sub bajo y fluctuante
        MusicFeatures f{};
        s = s * 1664525u + 1013904223u;
        const float jitter = static_cast<float>(s >> 8) / 16777216.0f;
        f.rms = 0.05f + 0.10f * jitter; f.subBandRatio = 0.02f + 0.05f * jitter;
        f.bodyBandRatio = 0.3f; f.density = 0.5f; f.transientRate = 0.05f * jitter;
        const Seen x = feed(a, f, 1.0f, &pp);
        edges += x.impactEdges; maxTension = std::max(maxTension, x.maxTension);
    }
    EXPECT_EQ(edges, 0);
    EXPECT_LT(maxTension, 0.05f);
}

TEST(MacroArcConductor, NonFiniteFeaturesDoNotPoison) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    MusicFeatures bad{};
    bad.rms = nan; bad.subBandRatio = inf; bad.bodyBandRatio = -inf; bad.density = nan;
    bad.transientRate = nan; bad.presenceRatio = inf; bad.airRatio = nan; bad.bassRatio = nan;
    feed(a, bad, 10.0f, &pp);
    const Seen s = feed(a, playing(), 15.0f, &pp);
    for (float v : {s.last.arousal, s.last.anticipation, s.last.tension, s.last.impact, s.last.openness}) {
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 1.0f);
    }
    EXPECT_GT(s.last.arousal, 0.5f);
}

TEST(MacroArcConductor, ModulateOpensFieldPushesSubOnImpactAndKeepsTheRest) {
    MacroArcConductor a; setup(a);
    bool pp = false;
    const SceneApply base = baseScene();
    constexpr float kCeil = 0.15f;
    feed(a, playing(), 15.0f, &pp);
    SceneApply m;
    for (int i = 0; i < 400; ++i) m = a.modulate(base, kCeil);
    EXPECT_GT(m.t.wfsSpread, base.t.wfsSpread + 0.03f);
    EXPECT_GT(m.t.hrtfDepth, base.t.hrtfDepth);
    EXPECT_GT(m.t.airHolography, base.t.airHolography);
    EXPECT_LE(m.t.airHolography, kCeil + 1e-6f);
    EXPECT_NEAR(m.t.subPunch, base.t.subPunch, 0.01f) << "el sub solo se mueve con el impacto";
    const float spreadBeforeGap = m.t.wfsSpread;

    // Hueco: el campo se recoge un poco (contraste previo al impacto).
    for (int i = 0; i < 160; ++i) { a.advance(gap(), kBlockMs); m = a.modulate(base, kCeil); }
    EXPECT_LT(m.t.wfsSpread, spreadBeforeGap - 0.02f);

    // Retorno: el impacto empuja el sub y vuelve a reposo.
    float maxSub = 0.0f;
    for (int i = 0; i < 400; ++i) {
        a.advance(playing(), kBlockMs);
        m = a.modulate(base, kCeil);
        maxSub = std::max(maxSub, m.t.subPunch);
    }
    EXPECT_GT(maxSub, base.t.subPunch + 0.10f);
    EXPECT_LE(maxSub, 0.85f);
    EXPECT_NEAR(m.t.subPunch, base.t.subPunch, 0.02f) << "tras el impacto el sub vuelve a la base";

    // Lo que el arco no debe tocar queda idéntico.
    EXPECT_EQ(m.t.targetIacc, base.t.targetIacc);
    EXPECT_EQ(m.t.eqTiltDb, base.t.eqTiltDb);
    EXPECT_EQ(m.t.dynamicsAmount, base.t.dynamicsAmount);
    EXPECT_EQ(m.t.warmth, base.t.warmth);
    EXPECT_EQ(m.t.envDepth, base.t.envDepth);
    EXPECT_EQ(m.t.vocalIntimacy, base.t.vocalIntimacy);
    EXPECT_GE(m.t.wfsSpread, 0.10f);
    EXPECT_LE(m.t.wfsSpread, 1.0f);
}

TEST(MacroArcConductor, SceneTargetsNeverStepAudibly) {
    MacroArcConductor a; setup(a);
    const SceneApply base = baseScene();
    uint32_t s = 7u;
    SceneApply prev = a.modulate(base);
    for (int i = 0; i < 6000; ++i) {
        s = s * 1664525u + 1013904223u;
        const float r = static_cast<float>(s >> 8) / 16777216.0f;
        a.advance(r < 0.5f ? playing() : gap(), kBlockMs);   // cambios bruscos a propósito
        const SceneApply m = a.modulate(base, 0.15f);
        EXPECT_LE(std::fabs(m.t.subPunch - prev.t.subPunch), 0.14f);
        EXPECT_LE(std::fabs(m.t.wfsSpread - prev.t.wfsSpread), 0.05f);
        EXPECT_LE(std::fabs(m.t.airHolography - prev.t.airHolography), 0.02f);
        EXPECT_LE(std::fabs(m.t.stageElevation - prev.t.stageElevation), 0.02f);
        EXPECT_LE(std::fabs(m.t.hrtfDepth - prev.t.hrtfDepth), 0.02f);
        prev = m;
    }
}

TEST(MacroArcConductor, HapticBoostIsBoundedAndOffWhenDisabled) {
    MacroArcConductor on; setup(on);
    bool pp = false;
    float maxBoost = 0.0f;
    feed(on, playing(), 12.0f, &pp);
    feed(on, gap(), 0.8f, &pp);
    for (int i = 0; i < 400; ++i) {
        on.advance(playing(), kBlockMs);
        const float b = on.hapticPunchBoost();
        EXPECT_GE(b, 0.0f);
        EXPECT_LE(b, 0.5f);
        maxBoost = std::max(maxBoost, b);
    }
    EXPECT_GT(maxBoost, 0.3f);
    MacroArcConductor off; setup(off, false, 1.0f);
    feed(off, playing(), 5.0f, &pp);
    EXPECT_EQ(off.hapticPunchBoost(), 0.0f);
}

// ═══ B) FourDExcitationChain real + extractor real ══════════════════════════

namespace {

constexpr int kSr = 48000;
constexpr int kBlk = 256;

// Tema sintético: bombo (barrido 150->60 Hz) cada 0.5 s + bajo sostenido + colchón; entre
// gapStart y gapEnd solo queda el colchón (el mismo material con el que se midió el extractor).
void fillBlock(std::vector<float>& l, std::vector<float>& r, long n0, float gapStart, float gapEnd,
               uint32_t& rng) {
    for (int i = 0; i < kBlk; ++i) {
        const float t = static_cast<float>(n0 + i) / kSr;
        const bool inGap = (t >= gapStart && t < gapEnd);
        const float tk = std::fmod(t, 0.5f);
        float s = 0.10f * std::sin(2.f * kPi * 440.f * t) + 0.06f * std::sin(2.f * kPi * 660.f * t);
        if (!inGap) {
            const float f = 60.f + 90.f * std::exp(-tk * 35.f);
            s += 0.7f * std::sin(2.f * kPi * f * tk) * std::exp(-tk * 14.f);
            s += 0.20f * std::sin(2.f * kPi * 55.f * t);
            rng = rng * 1664525u + 1013904223u;
            const float nz = static_cast<float>(rng >> 8) / 16777216.f * 2.f - 1.f;
            const float th = std::fmod(t + 0.25f, 0.5f);
            s += 0.12f * nz * std::exp(-th * 60.f);
        }
        l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = s;
    }
}

struct ChainRun {
    std::vector<SceneApply> scenes;      // lo publicado en el bus, un elemento por bloque
    std::vector<MacroArcOutput> arcs;
    std::vector<HapticFrame> haptics;
};

ChainRun runChain(bool touchArc, bool arcOn, float depth, bool haptics, float seconds,
                  float gapStart, float gapEnd) {
    FourDExcitationChain chain;
    chain.prepare(static_cast<float>(kSr), kBlk);
    chain.setEnabled(true);
    chain.setDepth(0.0f);                // el conductor de 5–10 ms no interviene: aislamos el arco
    if (haptics) { chain.setHapticsEnabled(true); chain.setHapticScale(0.85f); }
    if (touchArc) { chain.setArcEnabled(arcOn); chain.setArcDepth(depth); }
    std::vector<float> l(kBlk), r(kBlk);
    uint32_t rng = 1234567u;
    ChainRun out;
    const int blocks = static_cast<int>(seconds * kSr / kBlk);
    SceneApply base{};
    base.t.wfsSpread = 0.5f; base.t.hrtfDepth = 0.5f; base.t.subPunch = 0.10f;
    base.t.airHolography = 0.05f; base.t.stageElevation = 0.10f;
    base.gate = 0.9f; base.conf = 0.8f; base.styleIndex = 3;
    for (int b = 0; b < blocks; ++b) {
        fillBlock(l, r, static_cast<long>(b) * kBlk, gapStart, gapEnd, rng);
        chain.analyze(l.data(), r.data(), kBlk);
        chain.publishScene(base);
        out.scenes.push_back(SceneTargetBus::instance().peekLatest());
        out.arcs.push_back(chain.arc());
        out.haptics.push_back(chain.hapticFrame());
    }
    return out;
}

} // namespace

TEST(FourDChainArc, ArcOffIsBitExactToChainWithoutArc) {
    const ChainRun a = runChain(false, false, 0.0f, true, 6.0f, 99.0f, 99.0f);   // no se toca el arco
    const ChainRun b = runChain(true, true, 0.0f, true, 6.0f, 99.0f, 99.0f);     // activo, profundidad 0
    ASSERT_EQ(a.scenes.size(), b.scenes.size());
    for (size_t i = 0; i < a.scenes.size(); ++i) {
        ASSERT_TRUE(sameTargets(a.scenes[i].t, b.scenes[i].t)) << "bloque " << i;
        for (int k = 0; k < kHapticBandCount; ++k) {
            ASSERT_EQ(a.haptics[i].intensity[k], b.haptics[i].intensity[k]) << "bloque " << i << " banda " << k;
        }
    }
}

TEST(FourDChainArc, DetectsDropWithRealExtractorAndPushesSubOnReturn) {
    constexpr float kGapStart = 8.0f, kGapEnd = 9.2f, kTotal = 14.0f;
    const ChainRun off = runChain(false, false, 0.0f, true, kTotal, kGapStart, kGapEnd);
    const ChainRun on  = runChain(true, true, 1.0f, true, kTotal, kGapStart, kGapEnd);
    ASSERT_EQ(off.scenes.size(), on.scenes.size());

    const auto blockOf = [](float sec) { return static_cast<size_t>(sec * kSr / kBlk); };
    float maxTension = 0.0f;
    int edges = 0;
    bool prev = false;
    for (size_t i = blockOf(3.0f); i < on.arcs.size(); ++i) {
        if (i >= blockOf(kGapStart) && i < blockOf(kGapEnd + 0.1f)) maxTension = std::max(maxTension, on.arcs[i].tension);
        if (on.arcs[i].impactPulse && !prev) ++edges;
        prev = on.arcs[i].impactPulse;
    }
    EXPECT_GT(maxTension, 0.5f) << "el hueco real (sub 0.59 -> 0.04) debe subir la tensión";
    EXPECT_EQ(edges, 1) << "un solo impacto al regresar el pulso";

    // Tras el retorno el sub publicado supera al de la cadena sin arco; antes del hueco coinciden.
    float maxDelta = 0.0f;
    for (size_t i = blockOf(kGapEnd); i < blockOf(kGapEnd + 1.0f); ++i) {
        maxDelta = std::max(maxDelta, on.scenes[i].t.subPunch - off.scenes[i].t.subPunch);
    }
    EXPECT_GT(maxDelta, 0.05f);
    for (size_t i = blockOf(3.0f); i < blockOf(3.5f); ++i) {
        EXPECT_NEAR(on.scenes[i].t.subPunch, off.scenes[i].t.subPunch, 0.05f);
    }
    // La háptica nunca sale de rango con el refuerzo del impacto.
    for (const HapticFrame& h : on.haptics) {
        for (int k = 0; k < kHapticBandCount; ++k) {
            EXPECT_TRUE(std::isfinite(h.intensity[k]));
            EXPECT_GE(h.intensity[k], 0.0f);
            EXPECT_LE(h.intensity[k], 1.0f);
        }
    }
}

TEST(FourDChainArc, ArcFollowsMasterSwitch) {
    FourDExcitationChain chain;
    chain.prepare(static_cast<float>(kSr), kBlk);
    chain.setArcEnabled(true);
    chain.setArcDepth(1.0f);
    std::vector<float> l(kBlk), r(kBlk);
    uint32_t rng = 1u;
    for (int b = 0; b < 400; ++b) {          // cadena maestra APAGADA: el arco no avanza
        fillBlock(l, r, static_cast<long>(b) * kBlk, 99.f, 99.f, rng);
        chain.analyze(l.data(), r.data(), kBlk);
    }
    EXPECT_EQ(chain.arc().arousal, 0.0f);
    chain.setEnabled(true);                  // al encender el maestro, el arco (ya pedido) actúa
    for (int b = 0; b < 400; ++b) {
        fillBlock(l, r, static_cast<long>(b) * kBlk, 99.f, 99.f, rng);
        chain.analyze(l.data(), r.data(), kBlk);
    }
    EXPECT_GT(chain.arc().arousal, 0.1f);
}
