// test_stage_ceiling_continuity — tronidos/crujidos al subir volumen.
//
// Causa medida: SupremeAcousticStabilityGuard::enforceStageEnergyCeiling()
// aplicaba una ganancia constante por bloque sin memoria entre bloques. Con
// senal caliente el techo de pico (0.95) se activaba en unos bloques y en
// otros no, y la ganancia saltaba en escalon en la frontera de bloque.
// Estas pruebas miden la DISCONTINUIDAD DE GANANCIA en frontera de bloque
// (g_fin_bloque_N vs g_inicio_bloque_N+1) usando una senal de prueba cuyo
// valor "sin procesar" conocemos exactamente.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "../supreme/SupremeAcousticStabilityGuard.hpp"

using ivanna::supreme::SupremeAcousticStabilityGuard;
using ivanna::supreme::AcousticModuleId;

namespace {
constexpr size_t kBlock = 320;
constexpr float  kSr    = 48000.0f;

// Simula una etapa que REALZA la senal (exciter/EQ) con volumen alto:
// entrada a amplitud a, salida de la etapa a amplitud a*boost.
struct CeilRun { float maxBoundaryGainJump; float maxAbs; bool finite; };

CeilRun runAlternating(float loud, float quiet, float boost, int blocks) {
    SupremeAcousticStabilityGuard g;
    g.prepare(kSr);
    g.reset();
    std::vector<float> inL(kBlock), inR(kBlock), L(kBlock), R(kBlock);
    float prevEndGain = 1.0f;
    bool  havePrev = false;
    CeilRun r{0.0f, 0.0f, true};
    double phase = 0.0;
    for (int b = 0; b < blocks; ++b) {
        // Alterna bloques fuertes/suaves: cambio de cancion, golpe de bateria,
        // o subida de volumen donde el techo se activa de forma intermitente.
        const float amp = (b % 2 == 0) ? loud : quiet;
        for (size_t i = 0; i < kBlock; ++i) {
            const float s = amp * (float)std::sin(phase);
            phase += 2.0 * M_PI * 997.0 / kSr;
            inL[i] = s; inR[i] = s;
            L[i] = s * boost; R[i] = s * boost;
        }
        g.beginBlock(inL.data(), inR.data(), kBlock, true);
        g.enforceStageEnergyCeiling(AcousticModuleId::ObjectRenderer,
                                    L.data(), R.data(), kBlock, 1.25f, 0.95f);
        // Ganancia efectiva en muestras por debajo de la rodilla (0.85), donde
        // softCeiling es identidad: g = out / (in*boost).
        auto gainAt = [&](size_t i) -> float {
            const float ref = inL[i] * boost;
            if (std::fabs(ref) < 0.05f || std::fabs(L[i]) >= 0.84f) return NAN;
            return L[i] / ref;
        };
        float startGain = NAN, endGain = NAN;
        for (size_t i = 0; i < 40 && std::isnan(startGain); ++i) startGain = gainAt(i);
        for (size_t i = kBlock; i-- > kBlock - 40 && std::isnan(endGain);) endGain = gainAt(i);
        if (havePrev && !std::isnan(startGain)) {
            r.maxBoundaryGainJump = std::max(r.maxBoundaryGainJump, std::fabs(startGain - prevEndGain));
        }
        if (!std::isnan(endGain)) { prevEndGain = endGain; havePrev = true; }
        for (size_t i = 0; i < kBlock; ++i) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) r.finite = false;
            r.maxAbs = std::max(r.maxAbs, std::max(std::fabs(L[i]), std::fabs(R[i])));
        }
    }
    return r;
}
} // namespace

TEST(StageCeilingContinuity, SinEscalonDeGananciaEnFronteraDeBloque) {
    // Fuerte 0.9 x2 dispara el techo; suave 0.2 no. Antes: salto ~0.4-0.5.
    const CeilRun r = runAlternating(0.90f, 0.20f, 2.0f, 200);
    EXPECT_TRUE(r.finite);
    // Rampa continua: el salto entre la ultima muestra de un bloque y la
    // primera del siguiente es un solo paso de rampa (<1%).
    EXPECT_LT(r.maxBoundaryGainJump, 0.01f) << "salto=" << r.maxBoundaryGainJump;
}

TEST(StageCeilingContinuity, PicoSiempreAcotadoAlTecho) {
    const CeilRun r = runAlternating(1.0f, 0.95f, 3.0f, 200);
    EXPECT_TRUE(r.finite);
    EXPECT_LE(r.maxAbs, 0.95f + 1e-4f);
}

TEST(StageCeilingContinuity, SenalNormalPasaIntacta) {
    // Sin realce ni pico: ganancia exactamente 1, sin coloracion.
    const CeilRun r = runAlternating(0.30f, 0.10f, 1.0f, 50);
    EXPECT_TRUE(r.finite);
    EXPECT_LT(r.maxBoundaryGainJump, 1e-5f);
    EXPECT_NEAR(r.maxAbs, 0.30f, 1e-3f);
}

// Regresión del click residual a volumen alto: con ganancia de etapa == 1 la forma de
// onda sobre la rodilla (0.85) debe ser la MISMA curva softCeiling que con ganancia < 1.
// Antes, con g==1 la etapa dejaba pasar la senal sin comprimir y al alternar con g<1 las
// muestras entre 0.85 y el techo saltaban de valor en la frontera de bloque.
TEST(StageCeilingContinuity, FormaDeOndaSobreRodillaIndependienteDeLaGananciaDeEtapa) {
    SupremeAcousticStabilityGuard g;
    g.prepare(kSr);
    g.reset();
    std::vector<float> inL(kBlock), inR(kBlock), L(kBlock), R(kBlock);
    double phase = 0.0;
    float maxDev = 0.0f;
    bool checkedHot = false;
    for (int b = 0; b < 400; ++b) {
        // 0..9: etapa que realza x3 sobre senal fuerte -> el techo baja g < 1.
        // 10..399: sin realce, g se recupera hacia 1 (release ~60 ms) con pico 0.92 > rodilla.
        const bool hot = b < 10;
        const float amp = hot ? 0.9f : 0.92f;
        const float boost = hot ? 3.0f : 1.0f;
        for (size_t i = 0; i < kBlock; ++i) {
            const float s = amp * (float)std::sin(phase);
            phase += 2.0 * M_PI * 997.0 / kSr;
            inL[i] = s; inR[i] = s;
            L[i] = s * boost; R[i] = s * boost;
        }
        g.beginBlock(inL.data(), inR.data(), kBlock, true);
        g.enforceStageEnergyCeiling(AcousticModuleId::ObjectRenderer,
                                    L.data(), R.data(), kBlock, 1.25f, 0.95f);
        if (b == 399) {
            for (size_t i = 0; i < kBlock; ++i) {
                const float expect = SupremeAcousticStabilityGuard::softCeilingSample(inL[i], 0.85f, 0.95f);
                maxDev = std::max(maxDev, std::fabs(L[i] - expect));
                if (std::fabs(inL[i]) > 0.86f) checkedHot = true;
            }
        }
    }
    EXPECT_TRUE(checkedHot) << "la senal de prueba debe superar la rodilla";
    EXPECT_LT(maxDev, 1.0e-4f) << "desviacion de la curva softCeiling con g==1: " << maxDev;
}
