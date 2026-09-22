// test_ime_bridge.cpp — pruebas obligatorias del bucle IME (PASO 8).
#include "../music_intelligence/MusicIntelligenceEngine.hpp"
#include "../music_intelligence/ImeBridge.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

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

    reinterpret_cast<ivanna::ime::ImeSharedState*>(imeSharedOpaque())->enabled.store(true);
    static float buf[512 * 2];
    for (int i = 0; i < 512; ++i) {
        const float s = 0.5f * std::sin(2.f * 3.14159265f * 110.f * i / 48000.f);
        buf[2 * i] = s; buf[2 * i + 1] = s;
    }
    for (int b = 0; b < 40; ++b) imeFeedBlock(buf, 512);
    CHECK(reinterpret_cast<ivanna::ime::ImeSharedState*>(imeSharedOpaque())->blocksFed.load() >= 40, "feed incrementa blocksFed");
    char json[512];
    const int n = imeDecideNowJson(json, sizeof(json));
    CHECK(n > 0, "imeDecideNowJson produce JSON");
    CHECK(std::strstr(json, "\"style\":") && std::strstr(json, "\"blocks\":"), "JSON con style y blocks");

    if (failures == 0) std::printf("PASSED: test_ime_bridge OK\n");

    // ── Fase 2 (misión "validación de cadena completa"): la ruta FUNCIONAL
    //    real, no variables aisladas. Hasta aquí solo se probó decide() con
    //    MusicFeatures armados a mano (bypasea el extractor) y que
    //    imeFeedBlock+imeDecideNowJson producen *algo* (sin verificar que
    //    el contenido del audio importe). Aquí: audio real y DISTINTO
    //    (graves puros vs agudos puros) por el camino RT real completo
    //    (imeFeedBlock → MusicFeatureExtractor real → decide() → JSON) debe
    //    producir una decisión medible y distinta — la prueba de que la
    //    ruta funcional entera está viva, no solo sus piezas por separado.
    {
        auto* state = reinterpret_cast<ivanna::ime::ImeSharedState*>(imeSharedOpaque());
        state->enabled.store(true);

        auto feedToneAndDecide = [](float freqHz, char* outJson, int outSize) {
            static float toneBuf[512 * 2];
            for (int b = 0; b < 60; ++b) { // suficientes bloques para que el EMA del extractor converja
                for (int i = 0; i < 512; ++i) {
                    const float s = 0.6f * std::sin(2.f * 3.14159265f * freqHz *
                                                      (b * 512 + i) / 48000.f);
                    toneBuf[2 * i] = s; toneBuf[2 * i + 1] = s;
                }
                imeFeedBlock(toneBuf, 512);
            }
            return imeDecideNowJson(outJson, outSize);
        };

        char jsonBass[512], jsonTreble[512];
        const int nB = feedToneAndDecide(80.f,   jsonBass,   sizeof(jsonBass));   // grave puro
        const int nT = feedToneAndDecide(9000.f, jsonTreble, sizeof(jsonTreble)); // agudo puro
        CHECK(nB > 0 && nT > 0, "ruta funcional: ambas decisiones producen JSON");

        auto extractFloat = [](const char* json, const char* key) -> double {
            const char* p = std::strstr(json, key);
            return p ? std::atof(p + std::strlen(key)) : 0.0;
        };
        const double tiltBass   = extractFloat(jsonBass,   "\"eqTiltDb\":");
        const double tiltTreble = extractFloat(jsonTreble, "\"eqTiltDb\":");
        // eqTiltDb positivo realza agudos, negativo realza graves (semántica
        // de MusicIntelligenceEngine::decide) — un agudo puro debe inclinar
        // hacia arriba y un grave puro hacia abajo: no solo "distinto", sino
        // en la dirección que dicta el contenido real del audio.
        CHECK(tiltTreble > tiltBass,
              "ruta funcional: agudo puro produce eqTiltDb mayor que grave puro (no solo distinto, en la direccion correcta)");
        CHECK(std::strncmp(jsonBass, jsonTreble, sizeof(jsonBass)) != 0,
              "ruta funcional: el JSON completo difiere (no es un valor fijo disfrazado)");

        if (tiltTreble > tiltBass) std::printf("PASSED: test_ime_bridge (ruta funcional) OK\n");
    }
    return failures ? 1 : 0;
}
