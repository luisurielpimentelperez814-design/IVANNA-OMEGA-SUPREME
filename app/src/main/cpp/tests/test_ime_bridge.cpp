// test_ime_bridge.cpp — pruebas obligatorias del bucle IME (PASO 8).
#include "../music_intelligence/MusicIntelligenceEngine.hpp"
#include "../music_intelligence/ImeBridge.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace ivanna::ime;
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FALLARON: %s\n", msg); ++failures; } } while (0)

int main() {
    MusicIntelligenceEngine eng;
    MusicFeatures bass{};
    bass.rms=0.3f; bass.peak=0.9f; bass.crestDb=9.5f; bass.bassRatio=0.7f;
    bass.midRatio=0.2f; bass.trebleRatio=0.1f; bass.stereoWidth=0.3f;
    bass.transientRate=0.6f; bass.density=0.8f;
    MusicFeatures treble{};
    treble.rms=0.1f; treble.peak=0.15f; treble.crestDb=3.5f; treble.bassRatio=0.05f;
    treble.midRatio=0.25f; treble.trebleRatio=0.7f; treble.stereoWidth=0.9f;
    treble.transientRate=0.1f; treble.density=0.3f;

    MusicDecision d1 = eng.decide(bass);
    MusicDecision d2 = eng.decide(treble);
    CHECK(d1.profileIndex >= 0 && d2.profileIndex >= 0, "decide devuelve perfil valido");
    CHECK(d1.profileIndex != d2.profileIndex, "entradas distintas producen decisiones distintas");
    MusicDecision d1b = eng.decide(bass);
    CHECK(d1b.profileIndex == d1.profileIndex && d1b.confidence == d1.confidence, "decide es determinista");
    CHECK(std::isfinite(d1.wfsSpread) && std::isfinite(d1.hrtfDepth) && std::isfinite(d1.eqTiltDb)
       && std::isfinite(d1.dynamicsAmount) && std::isfinite(d1.envDepth), "decision sin NaN");

    imeShared().enabled.store(true);
    static float buf[512 * 2];
    for (int i = 0; i < 512; ++i) {
        const float s = 0.5f * std::sin(2.f * 3.14159265f * 110.f * i / 48000.f);
        buf[2 * i] = s; buf[2 * i + 1] = s;
    }
    for (int b = 0; b < 40; ++b) imeFeedBlock(buf, 512);
    CHECK(imeShared().blocksFed.load() >= 40, "feed incrementa blocksFed");
    char json[512];
    const int n = imeDecideNowJson(json, sizeof(json));
    CHECK(n > 0, "imeDecideNowJson produce JSON");
    CHECK(std::strstr(json, "\"style\":") && std::strstr(json, "\"blocks\":"), "JSON con style y blocks");

    if (failures == 0) std::printf("PASSED: test_ime_bridge OK\n");
    return failures ? 1 : 0;
}
