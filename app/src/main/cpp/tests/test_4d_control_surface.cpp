// test_4d_control_surface.cpp — Puerta host de la superficie global de la Cadena 4D.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Ejercita el MISMO camino que usa el dispositivo (ensureInit / setEnabled / feedPlanar /
// applyOutputPlanar / publishAnticipatedScene / status) sobre la cadena y el extractor
// REALES: 1) apagado = identidad estricta; 2) apertura anticipada con señal periódica
// (golpe 1 de cada 50 bloques = 120 BPM); 3) techo háptico; 4) dosis finita;
// 5) publicación real en el SceneTargetBus.
#include <gtest/gtest.h>

#include "music_intelligence/FourDControlSurface.hpp"
#include "music_intelligence/SceneTargetBus.hpp"

#include <cmath>
#include <limits>
#include <vector>

using namespace ivanna;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int   kSr = 48000;
constexpr int   kBlock = 480;      // 10 ms
constexpr int   kBeatBlocks = 50;  // 500 ms @ 120 BPM

// Señal periódica REAL: un golpe (click + cuerpo 60 Hz) en el bloque 0 de cada compás y
// un colchón tenue en los 49 bloques restantes. La rejilla del golpe NO se repite cada
// bloque (si cada bloque fuese idéntico, transientRate quedaría plano y no habría onset).
void fillBeatBlock(std::vector<float>& l, std::vector<float>& r, int phaseBlock) {
    for (int i = 0; i < kBlock; ++i) {
        const float tt = static_cast<float>(i) / kSr;
        float s;
        if (phaseBlock == 0) {
            const float click = (tt < 0.002f) ? std::exp(-tt * 1500.f) : 0.f;
            const float body  = std::sin(2.f * kPi * 60.f * tt) * std::exp(-tt * 18.f);
            s = 0.9f * (0.6f * body + 0.4f * click);
        } else {
            s = 0.015f * std::sin(2.f * kPi * 300.f * tt);
        }
        l[i] = r[i] = s;
    }
}

// Material realista a 120 BPM: bombo con barrido de tono y cuerpo ~110 ms + hats de ruido a
// contratiempo. Con un clic aislado la tasa de transitorios del extractor queda tan baja que la
// compuerta de confianza no abre (medido); con material rítmico real sí abre (confianza ~1).
// `n0` es la muestra global del primer frame para que la señal sea continua entre bloques.
void fillKickHatsBlock(std::vector<float>& l, std::vector<float>& r, long n0, unsigned& rng) {
    for (int i = 0; i < kBlock; ++i) {
        const float t  = static_cast<float>(n0 + i) / kSr;
        const float tk = std::fmod(t, 0.5f);
        const float kick = std::sin(2.f * kPi * (55.f + 80.f * std::exp(-tk * 40.f)) * tk)
                         * std::exp(-tk * 9.f) * 0.8f;
        const float th = std::fmod(t + 0.25f, 0.5f);
        rng = rng * 1664525u + 1013904223u;
        const float nz = static_cast<float>((rng >> 8) & 0xFFFF) / 32768.f - 1.f;
        l[i] = r[i] = kick + nz * std::exp(-th * 60.f) * 0.25f;
    }
}
} // namespace

TEST(FourDControlSurface, InitializeIsIdempotent) {
    fourd::ensureInit(48000.f, kBlock);
    EXPECT_TRUE(fourd::isInitialized());
    fourd::ensureInit(48000.f, kBlock);
    EXPECT_TRUE(fourd::isInitialized());
}

TEST(FourDControlSurface, DisabledIsBitExact) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(false);
    std::vector<float> l(kBlock), r(kBlock);
    for (int i = 0; i < kBlock; ++i) { l[i] = std::sin(0.013f * i) * 0.5f; r[i] = std::cos(0.021f * i) * 0.4f; }
    const std::vector<float> l0 = l, r0 = r;
    fourd::applyOutputPlanar(l.data(), r.data(), kBlock);
    for (int i = 0; i < kBlock; ++i) { EXPECT_FLOAT_EQ(l[i], l0[i]); EXPECT_FLOAT_EQ(r[i], r0[i]); }
    EXPECT_FALSE(fourd::status().enabled);
}

TEST(FourDControlSurface, PredictsOnsetOn120Bpm) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);

    std::vector<float> l(kBlock), r(kBlock);
    bool  armedEver = false;
    float lastPeriod = 0.f;

    for (int b = 0; b < kBeatBlocks * 12; ++b) {
        fillBeatBlock(l, r, b % kBeatBlocks);
        fourd::feedPlanar(l.data(), r.data(), kBlock);
        const auto st = fourd::status();
        if (st.armed && st.periodMs > 0.f) { armedEver = true; lastPeriod = st.periodMs; }
    }
    EXPECT_TRUE(fourd::status().enabled);
    EXPECT_TRUE(armedEver) << "el conductor no se armó con un patrón estable de 120 BPM";
    EXPECT_GT(lastPeriod, 350.f);
    EXPECT_LT(lastPeriod, 650.f);
}

TEST(FourDControlSurface, HapticsRespectHardCeiling) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setHapticsEnabled(true);
    fourd::setHapticScale(0.85f);
    std::vector<float> l(kBlock), r(kBlock);
    for (int b = 0; b < kBeatBlocks * 6; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const float tt = static_cast<float>(i) / kSr;
            const float click = (tt < 0.002f) ? std::exp(-tt * 1500.f) : 0.f;
            const float body  = std::sin(2.f * kPi * 60.f * tt) * std::exp(-tt * 18.f);
            l[i] = r[i] = 0.95f * (0.6f * body + 0.4f * click);
        }
        fourd::feedPlanar(l.data(), r.data(), kBlock);
        const auto st = fourd::status();
        EXPECT_LE(st.hapticAmplitude, 255);
        EXPECT_TRUE(std::isfinite(st.hapticTotal));
        EXPECT_LE(st.hapticTotal, 4.0f);
    }
}

TEST(FourDControlSurface, DosimeterAccumulates) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);
    fourd::setDosimeterCalibration(100.0f);
    std::vector<float> l(kBlock), r(kBlock);
    for (int i = 0; i < kBlock; ++i) { const float v = 0.5f * std::sin(2.f * kPi * 1000.f * i / kSr); l[i] = r[i] = v; }
    for (int b = 0; b < 1000; ++b) fourd::feedPlanar(l.data(), r.data(), kBlock);
    const auto st = fourd::status();
    EXPECT_GT(st.exposureDose, 0.f);
    EXPECT_TRUE(std::isfinite(st.exposureLaeq));
    EXPECT_GT(st.exposureLaeq, 0.f);
}

TEST(FourDControlSurface, PublishesAnticipatedScene) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    std::vector<float> l(kBlock), r(kBlock);
    for (int i = 0; i < kBlock; ++i) { const float v = 0.3f * std::sin(2.f * kPi * 220.f * i / kSr); l[i] = v; r[i] = -v; }
    fourd::feedPlanar(l.data(), r.data(), kBlock);
    const uint32_t before = fourd::publishedScenes();
    fourd::publishAnticipatedScene();
    EXPECT_GT(fourd::publishedScenes(), before);
    EXPECT_GE(ime::SceneTargetBus::instance().peekLatest().seq, 1u);
}

// Regresión del defecto de realimentación: publishAnticipatedScene() usaba como base la escena
// del bus, que tras la primera publicación ES su propia salida modulada; la apertura se acumulaba
// bloque a bloque hasta saturar. La modulación debe quedar ACOTADA sobre la base ajena Y ser real
// (que la compuerta abra: si no, la prueba sería vacua y no detectaría el defecto).
TEST(FourDControlSurface, SceneModulationDoesNotCompoundOnOwnOutput) {
    fourd::shutdown();
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDepth(1.0f);
    std::vector<float> l(kBlock), r(kBlock);

    // Base ajena estable, como la que publica el StyleBlender.
    ime::SceneApply base{};
    base.t.subPunch = 0.10f;
    base.t.wfsSpread = 0.30f;
    base.gate = 1.0f;
    base.conf = 0.9f;
    ime::SceneTargetBus::instance().publish(base);

    unsigned rng = 12345u;
    float maxSub = 0.0f;
    for (int b = 0; b < kBeatBlocks * 12; ++b) {
        fillKickHatsBlock(l, r, static_cast<long>(b) * kBlock, rng);
        fourd::feedPlanar(l.data(), r.data(), kBlock);
        fourd::publishAnticipatedScene();
        const float s = ime::SceneTargetBus::instance().peekLatest().t.subPunch;
        if (s > maxSub) maxSub = s;
    }
    EXPECT_GT(fourd::publishedScenes(), 100u);
    // La modulación debe ocurrir (compuerta abierta con material rítmico real)...
    EXPECT_GT(maxSub, 0.10f + 0.05f) << "la cadena no modula: la compuerta no abrió con ritmo real";
    // ...y estar acotada: máx. de UNA publicación = base + 0.35*gate*depth*anticipación (<=1).
    // Con la realimentación subía hasta el techo de 0.85 de la modulación.
    EXPECT_LE(maxSub, 0.10f + 0.35f + 1e-3f) << "la apertura se acumula sobre su propia salida";
}

// Una base ajena NUEVA debe tomarse como base (no quedarse con la caché vieja): tras varias
// publicaciones (el suavizado de ~25 ms converge), la salida sigue a la nueva base.
TEST(FourDControlSurface, FreshForeignSceneBecomesTheNewBase) {
    fourd::shutdown();
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDepth(1.0f);
    std::vector<float> l(kBlock), r(kBlock);
    unsigned rng = 777u;
    long n0 = 0;
    auto feed = [&]() { fillKickHatsBlock(l, r, n0, rng); n0 += kBlock; fourd::feedPlanar(l.data(), r.data(), kBlock); };

    ime::SceneApply a{};  a.t.subPunch = 0.10f; a.gate = 1.0f; a.conf = 0.9f;
    ime::SceneTargetBus::instance().publish(a);
    for (int i = 0; i < 200; ++i) { feed(); fourd::publishAnticipatedScene(); }

    ime::SceneApply b = a;  b.t.subPunch = 0.40f;
    ime::SceneTargetBus::instance().publish(b);
    float minAfter = 1.0f;
    for (int i = 0; i < 100; ++i) {
        feed();
        fourd::publishAnticipatedScene();
        if (i >= 60) minAfter = std::min(minAfter, ime::SceneTargetBus::instance().peekLatest().t.subPunch);
    }
    EXPECT_GE(minAfter, 0.40f - 0.03f) << "se quedó con la base vieja en lugar de la nueva";
}

// Defecto del suavizado: al activarse por primera vez partía de un estado a 0 y la salida se
// hundía hacia 0 antes de subir. Debe partir de la escena vigente (base) y no hundirse.
TEST(ExcitationConductor, FirstActivationStartsFromBaseNotFromZero) {
    ime::ExcitationConductor c;
    c.prepare(48000.f, kBlock);
    c.setEnabled(true);
    c.setDepth(1.0f);
    const float blockMs = static_cast<float>(kBlock) / kSr * 1000.0f;
    ime::MusicFeatures f{};
    f.bassRatio = 0.6f; f.crestDb = 12.f; f.flatness1m = 0.3f;
    // Pulso periódico de 500 ms con tasa de transitorios alta: arma el conductor con confianza alta.
    for (int b = 0; b < kBeatBlocks * 8; ++b) {
        f.transientRate = ((b % kBeatBlocks) == 0) ? 0.8f : 0.3f;
        c.advance(f, blockMs);
    }
    ASSERT_TRUE(c.output().armed);
    ASSERT_GT(c.output().gate * c.output().anticipation, 0.05f) << "el conductor no está activo en este punto del compás";

    ime::SceneApply base{};
    base.t.subPunch = 0.60f;
    base.t.wfsSpread = 0.50f;
    const ime::SceneApply out = c.modulate(base, 0.15f);
    EXPECT_GE(out.t.subPunch, 0.60f - 0.05f) << "la salida se hundió hacia 0 al activarse";
    EXPECT_GE(out.t.wfsSpread, 0.50f - 0.05f);
}

// Con la salida medida, el dosímetro refleja la señal FINAL y no la entrada seca.
TEST(FourDControlSurface, OutputMeteringOverridesDryInput) {
    fourd::ensureInit(48000.f, kBlock);   // cadena nueva por puerta de re-preparación idéntica: se reinicia abajo
    fourd::shutdown();
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);
    fourd::setDosimeterCalibration(100.0f);

    std::vector<float> quiet(kBlock), loud(kBlock);
    for (int i = 0; i < kBlock; ++i) {
        const float v = std::sin(2.f * kPi * 1000.f * i / kSr);
        quiet[i] = 0.001f * v;   // entrada seca muy baja
        loud[i]  = 0.5f   * v;   // salida final fuerte (la cadena DSP subió el nivel)
    }
    for (int b = 0; b < 1000; ++b) {
        fourd::feedPlanar(quiet.data(), quiet.data(), kBlock);
        fourd::meterOutputPlanar(loud.data(), loud.data(), kBlock);
    }
    const float laeqOut = fourd::status().exposureLaeq;

    fourd::shutdown();
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);
    fourd::setDosimeterCalibration(100.0f);
    for (int b = 0; b < 1000; ++b) fourd::feedPlanar(quiet.data(), quiet.data(), kBlock);
    const float laeqDry = fourd::status().exposureLaeq;

    EXPECT_GT(laeqOut, laeqDry + 20.0f) << "el dosímetro no está midiendo la salida final";
}

// Si la medición de salida se detiene, el dosímetro vuelve solo a medir la entrada.
TEST(FourDControlSurface, OutputMeteringFallsBackToInputWhenStopped) {
    fourd::shutdown();
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);
    fourd::setDosimeterCalibration(100.0f);
    std::vector<float> loud(kBlock);
    for (int i = 0; i < kBlock; ++i) loud[i] = 0.5f * std::sin(2.f * kPi * 1000.f * i / kSr);

    fourd::meterOutputPlanar(loud.data(), loud.data(), kBlock);       // arma el sostén
    const float doseBefore = fourd::status().exposureDose;
    for (int b = 0; b < 200; ++b) fourd::feedPlanar(loud.data(), loud.data(), kBlock);  // sin más medición de salida
    EXPECT_GT(fourd::status().exposureDose, doseBefore) << "no volvió a medir la entrada";
}

// Un bloque con NaN/Inf no debe envenenar el estado de la cadena (extractor con memoria).
TEST(FourDControlSurface, NonFiniteInputDoesNotPoisonState) {
    fourd::shutdown();
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setDosimeterMode(0);
    fourd::setDosimeterCalibration(100.0f);
    std::vector<float> l(kBlock), r(kBlock), bad(kBlock);
    unsigned rng = 99u;
    for (int i = 0; i < kBlock; ++i) bad[i] = (i % 7 == 0) ? std::nanf("") : std::numeric_limits<float>::infinity();
    long n0 = 0;
    for (int b = 0; b < kBeatBlocks * 6; ++b) {
        fillKickHatsBlock(l, r, n0, rng); n0 += kBlock;
        if (b % 5 == 0) {   // intercala bloques envenenados en entrada y en medición de salida
            fourd::feedPlanar(bad.data(), bad.data(), kBlock);
            fourd::meterOutputPlanar(bad.data(), bad.data(), kBlock);
        }
        fourd::feedPlanar(l.data(), r.data(), kBlock);
        fourd::publishAnticipatedScene();
    }
    const auto st = fourd::status();
    EXPECT_TRUE(std::isfinite(st.confidence));
    EXPECT_TRUE(std::isfinite(st.anticipation));
    EXPECT_TRUE(std::isfinite(st.exposureDose));
    EXPECT_TRUE(std::isfinite(st.exposureLaeq));
    EXPECT_TRUE(std::isfinite(st.hapticTotal));
    EXPECT_TRUE(st.armed) << "los bloques válidos deberían seguir armando el conductor";
}

// ── Arco narrativo a través de la superficie global (mismo camino que JNI/UI) ─────────────────
// Apagado: ni un solo estado del arco se mueve. Encendido (profundidad 1): la excitación sube
// con material rítmico real. Y los controles sobreviven a un shutdown()/ensureInit(): la
// superficie los reaplica (la cadena nueva arranca con el arco apagado y profundidad 0).
TEST(FourDControlSurface, ArcControlsReachTheChainAndSurviveReinit) {
    fourd::ensureInit(48000.f, kBlock);
    fourd::setEnabled(true);
    fourd::setArcDepth(1.0f);
    fourd::setArcEnabled(false);

    std::vector<float> l(kBlock), r(kBlock);
    unsigned rng = 77u;
    long n = 0;
    auto feedSeconds = [&](int blocks) {
        for (int b = 0; b < blocks; ++b) {
            fillKickHatsBlock(l, r, n, rng);
            n += kBlock;
            fourd::feedPlanar(l.data(), r.data(), kBlock);
        }
    };

    feedSeconds(400);   // 4 s con el arco apagado
    EXPECT_EQ(fourd::status().arcArousal, 0.0f);
    EXPECT_EQ(fourd::status().arcPhase, 0);

    fourd::setArcEnabled(true);
    feedSeconds(600);   // 6 s encendido
    EXPECT_GT(fourd::status().arcArousal, 0.1f);

    fourd::shutdown();                 // la cadena se libera y se rehace
    fourd::ensureInit(48000.f, kBlock);
    feedSeconds(600);
    EXPECT_GT(fourd::status().arcArousal, 0.1f) << "los controles del arco no se reaplicaron tras reiniciar";

    fourd::setArcEnabled(false);       // no dejar estado a otros tests
    fourd::setArcDepth(0.0f);
}
