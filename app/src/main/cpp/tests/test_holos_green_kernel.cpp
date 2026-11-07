// test_holos_green_kernel.cpp — G_b: la función de Green unificada.
//
// Prueba del teorema de unificación (§3 de FUSION.md) para lo espacial:
// cada módulo legacy es HOLOS con todo apagado menos una cosa. Aquí se
// verifica que G_b degenera en cada uno: Woodworth/campo lejano (HRTF),
// ILD esférico (ORBE), ley cilíndrica γ=½ (WFS), crossfeed de baja
// frecuencia, y que la pinna interpolada es exacta en los nudos.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "../spatial/HolosGreenKernel.hpp"
#include "../spatial/NearFieldHeadModel.hpp"

using namespace ivanna::holos;
using namespace ivanna::nearfield;

namespace {
constexpr float kTolUs = 5e-6f;  // ±5 µs en ITD (FUSION §9.1)
}

TEST(HolosGreen, FarFieldIsWoodworth) {
    // Campo lejano (r=100 m): el ITD esférico coincide con Woodworth
    // dentro del 1 % (mismo criterio que test_near_field_head_model:
    // Woodworth es una aproximación; el modelo de rayos difiere ~0.85 %
    // a 90°).
    for (float deg : {0.0f, 15.0f, 30.0f, 60.0f, 90.0f}) {
        const float th = deg * kPiF / 180.0f;
        const float itd = greenGains(th, 100.0f, 1000.0f, 1.0f, 0.0f, 0.0f).itdSeconds;
        const float ww = itdWoodworthFarField(th);
        EXPECT_NEAR(itd, ww, 0.01f * std::max(ww, 1e-6f)) << "theta=" << deg << "°";
    }
    // Valores de referencia: 0.261 ms @30°, 0.656 ms @90°.
    EXPECT_NEAR(greenGains(30.0f * kPiF / 180.0f, 100.0f, 1000.0f, 1.0f, 0.0f, 0.0f).itdSeconds,
                0.000261f, 0.01f * 0.000261f);
    EXPECT_NEAR(greenGains(90.0f * kPiF / 180.0f, 100.0f, 1000.0f, 1.0f, 0.0f, 0.0f).itdSeconds,
                0.000656f, 0.01f * 0.000656f);
}

TEST(HolosGreen, ItdGrowsWhenNear) {
    // A 90°, el ITD crece al acercarse (~+12% a 0.15 m, como ORBE).
    const float th = 90.0f * kPiF / 180.0f;
    const float far = greenGains(th, 2.0f, 1000.0f, 1.0f, 0.0f, 0.0f).itdSeconds;
    const float near = greenGains(th, 0.15f, 1000.0f, 1.0f, 0.0f, 0.0f).itdSeconds;
    EXPECT_GT(near, far);
    EXPECT_NEAR(near, 0.00073f, 40e-6f);
    // Simetría: fuente a la izquierda → ITD negativo.
    EXPECT_LT(greenGains(-th, 0.35f, 1000.0f, 1.0f, 0.0f, 0.0f).itdSeconds, 0.0f);
    // Centro: ITD cero.
    EXPECT_NEAR(greenGains(0.0f, 0.35f, 1000.0f, 1.0f, 0.0f, 0.0f).itdSeconds, 0.0f, 1e-9f);
}

TEST(HolosGreen, IldNearFarLimits) {
    // Cerca y a la derecha, en agudos: ILD grande (16–22 dB @0.15 m/90°/1500 Hz).
    const float ildNear = greenGains(90.0f * kPiF / 180.0f, 0.15f, 1500.0f, 1.0f, 0.0f, 0.0f).ildDb;
    EXPECT_GT(ildNear, 16.0f);
    EXPECT_LT(ildNear, 22.0f);
    // Lejos y en graves: ILD casi nulo (<2 dB @2 m/100 Hz/90°).
    EXPECT_LT(std::fabs(greenGains(90.0f * kPiF / 180.0f, 2.0f, 100.0f, 1.0f, 0.0f, 0.0f).ildDb),
              2.0f);
    // Centro: ILD cero.
    EXPECT_NEAR(greenGains(0.0f, 0.35f, 1000.0f, 1.0f, 0.0f, 0.0f).ildDb, 0.0f, 1e-6f);
}

TEST(HolosGreen, GammaHalfIsWfsCylindricalLaw) {
    // γ=1 → ILD esférico exacto (el de NearFieldHeadModel).
    // γ=½ → el término de divergencia se reduce a la mitad (ley 1/√d del
    // WFS 2.5D); la sombra cefálica no cambia.
    const float th = 60.0f * kPiF / 180.0f;
    for (float f : {0.0f, 100.0f, 1000.0f, 4000.0f}) {
        const float g1 = ildDbGamma(th, 0.5f, f, 1.0f);
        const float g05 = ildDbGamma(th, 0.5f, f, 0.5f);
        EXPECT_NEAR(g1, ildDb(th, 0.5f, f), 1e-5f) << "f=" << f;
        const float spreading = ildDb(th, 0.5f, 0.0f);
        const float shadow = ildDb(th, 0.5f, f) - spreading;
        EXPECT_NEAR(g05, 0.5f * spreading + shadow, 1e-5f) << "f=" << f;
    }
}

TEST(HolosGreen, CrossfeedIsLowFrequencyLimit) {
    // El crossfeed es G en el límite de baja frecuencia (f<700 Hz,
    // θ=30° fijo): ITD de Woodworth a 30° = 0.261 ms, el mismo que
    // HeadphoneCrossfeed (kItdSeconds = 0.000261 s).
    const float itd = greenGains(30.0f * kPiF / 180.0f, 1.0f, 100.0f, 1.0f, 0.0f, 0.0f).itdSeconds;
    EXPECT_NEAR(itd, 0.000261f, kTolUs);
}

TEST(HolosGreen, PowerConstantNormalization) {
    // La parte común se cancela: gL²+gR² == 2 en todo θ/r/f (HOLOS nunca
    // sube el nivel global, FUSION §5.4).
    for (float deg : {-90.0f, -45.0f, 0.0f, 30.0f, 90.0f})
        for (float r : {0.15f, 0.35f, 1.0f, 2.0f})
            for (float f : {100.0f, 1000.0f, 8000.0f}) {
                const GreenGains g = greenGains(deg * kPiF / 180.0f, r, f, 1.0f, 0.0f, 3.0f);
                EXPECT_NEAR(g.gL * g.gL + g.gR * g.gR, 2.0f, 1e-5f)
                    << "deg=" << deg << " r=" << r << " f=" << f;
                EXPECT_GT(g.gL, 0.0f);
                EXPECT_GT(g.gR, 0.0f);
            }
}

TEST(HolosGreen, ProximityShelfCappedAndLowOnly) {
    // Estante de proximidad: capado a 6 dB (a 20 Hz la forma es ~1.0), y
    // 5.4 dB @0.15 m/100 Hz por la forma 1/(1+(f/300)²) (mismo valor medido
    // en ORBE). Solo en graves: <1 dB @4 kHz.
    const GreenGains gCap = greenGains(0.0f, 0.15f, 20.0f, 1.0f, 0.0f, 6.0f);
    EXPECT_NEAR(gCap.proximityDb, 6.0f, 0.1f);
    const GreenGains gLow = greenGains(0.0f, 0.15f, 100.0f, 1.0f, 0.0f, 6.0f);
    EXPECT_NEAR(gLow.proximityDb, 5.4f, 0.1f);
    const GreenGains gHigh = greenGains(0.0f, 0.15f, 4000.0f, 1.0f, 0.0f, 6.0f);
    EXPECT_LT(gHigh.proximityDb, 1.0f);
    // A r ≥ 1 m es cero.
    EXPECT_NEAR(greenGains(0.0f, 1.5f, 100.0f, 1.0f, 0.0f, 6.0f).proximityDb, 0.0f, 1e-6f);
}

TEST(HolosGreen, PinnaTableEmptyIsIdentity) {
    PinnaTable t;
    EXPECT_TRUE(t.empty());
    EXPECT_NEAR(t.pinnaDb(0.5f, 8000.0f), 0.0f, 1e-9f);
    // Con tabla cargada, la ganancia de G incluye la pinna diferencial.
    const GreenGains g0 = greenGains(0.5f, 1.0f, 8000.0f, 1.0f, 0.0f, 0.0f);
    const GreenGains g1 = greenGains(0.5f, 1.0f, 8000.0f, 1.0f, 6.0f, 0.0f);
    EXPECT_GT(g1.gR / g1.gL, g0.gR / g0.gL);
}

TEST(HolosGreen, PinnaTableLinearInterpolation) {
    PinnaTable t;
    // Nudos sintéticos (NO son datos medidos: solo prueban la interpolación).
    const std::vector<float> az = {-1.0f, 0.0f, 1.0f};
    const std::vector<float> fr = {1000.0f, 8000.0f};
    // mag[az][f]: 0 en −1 rad, 6 dB en 0 rad, 12 dB en +1 rad (igual en f).
    const std::vector<float> mag = {0.0f, 0.0f, 6.0f, 6.0f, 12.0f, 12.0f};
    t.set(az, fr, mag);
    EXPECT_FALSE(t.empty());
    EXPECT_NEAR(t.pinnaDb(-1.0f, 1000.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(t.pinnaDb(0.0f, 8000.0f), 6.0f, 1e-6f);
    EXPECT_NEAR(t.pinnaDb(1.0f, 4000.0f), 12.0f, 1e-6f);
    EXPECT_NEAR(t.pinnaDb(0.5f, 2000.0f), 9.0f, 1e-5f);  // punto medio en θ
    // Fuera de rango: clamp al borde.
    EXPECT_NEAR(t.pinnaDb(2.0f, 8000.0f), 12.0f, 1e-6f);
}

TEST(HolosGreen, DiffuseMixFromCoherence) {
    EXPECT_NEAR(diffuseMix(1.0f, 0.5f), 0.0f, 1e-9f);   // puntual → directo
    EXPECT_NEAR(diffuseMix(0.0f, 0.5f), 0.5f, 1e-9f);   // difuso → cola
    EXPECT_NEAR(diffuseMix(0.0f, 1.0f), 1.0f, 1e-9f);
    EXPECT_NEAR(diffuseMix(0.5f, 1.0f), 0.5f, 1e-9f);
    EXPECT_NEAR(diffuseMix(0.0f, 0.0f), 0.0f, 1e-9f);   // diffuse=0 → sin cola
}
