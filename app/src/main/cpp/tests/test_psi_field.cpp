// test_psi_field.cpp — FORMULACIÓN MAGISTRAL Ψ (2026-11-10).
//
// Verifica spatial/PsiField.hpp (Ecs. 2, 3, 4) y la extensión con pérdida
// de BioControl5D::update() (Ec. 4, EDO completa):
//
//   Ec. 2: ILD_total = SPREAD(r,θ) + SHADOW(f,θ) — la descomposición
//     exacta, calibrada contra el modelo geométrico NearFieldHeadModel.
//     La primera versión de la formulación (f/f0) era incorrecta y se
//     corrigió aquí: el test la habría cazado (error hasta 19.8 dB).
//   Ec. 3: operador de personalización P_l^m(a) — 1.0 en referencia,
//     sensible al ancho de cabeza, clampado [0.5, 2.0].
//   Ec. 4: Γ(η) y la EDO dη/dt = (η*−η)/τ − λ·L — con L=0 idéntica al
//     comportamiento legacy; con L>0, η converge más bajo.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "../spatial/BioControl5D.hpp"
#include "../spatial/NearFieldHeadModel.hpp"
#include "../spatial/PsiField.hpp"
#include "../spatial/PsiFieldRuntime.hpp"

using ivanna::psi::CipicAnthropometry;

namespace {

// ── Ec. 2: psi::ildDb == modelo geométrico (radio por defecto) ──────────────
TEST(PsiField, IldMatchesGeometricModel) {
    const float rs[] = {0.15f, 0.30f, 0.50f, 1.0f, 2.0f};
    const float thetas[] = {-1.5708f, -0.7854f, -0.2618f, 0.0f, 0.2618f, 0.7854f, 1.5708f};
    const float freqs[] = {100.0f, 500.0f, 1000.0f, 4000.0f, 8000.0f};
    for (float r : rs) {
        for (float th : thetas) {
            for (float f : freqs) {
                const float psi = ivanna::psi::ildDb(r, th, f);
                const float geo = ivanna::nearfield::ildDb(th, r, f);
                EXPECT_NEAR(psi, geo, 0.01) << "r=" << r << " th=" << th << " f=" << f;
            }
        }
    }
}

// ── Ec. 2: tabla de calibración F1 (θ=90°, f=1 kHz) ─────────────────────────
TEST(PsiField, DistanceLawCalibrationTable) {
    // Valores del modelo geométrico verificado en host (2026-11-10):
    // r=0.15 → 15.90, 0.30 → 9.53, 0.50 → 7.38, 1.00 → 5.83, 2.00 → 5.07 dB.
    const float rs[] = {0.15f, 0.30f, 0.50f, 1.0f, 2.0f};
    const float expected[] = {15.90f, 9.53f, 7.38f, 5.83f, 5.07f};
    const float th = 1.5707963f;  // 90°
    float prev = 1e9f;
    for (int i = 0; i < 5; ++i) {
        const float v = ivanna::psi::ildDb(rs[i], th, 1000.0f);
        EXPECT_NEAR(v, expected[i], 0.05) << "r=" << rs[i];
        EXPECT_LT(v, prev) << "el ILD debe caer con la distancia (r=" << rs[i] << ")";
        prev = v;
    }
}

// ── Ec. 2: la descomposición es exacta (spread ⊥ freq, shadow ⊥ distancia) ──
TEST(PsiField, DecompositionIsExact) {
    const float th = 1.5707963f;  // 90°
    // SPREAD no depende de la frecuencia: mismo valor a 100 Hz y 8 kHz.
    for (float r : {0.15f, 0.5f, 2.0f}) {
        const float s1 = ivanna::psi::spreadIldDb(r, th);
        EXPECT_GT(s1, 0.0f) << "r=" << r;
        const float total500 = ivanna::psi::ildDb(r, th, 500.0f);
        const float total4000 = ivanna::psi::ildDb(r, th, 4000.0f);
        // La diferencia total(4000)−total(500) es pura sombra: igual en
        // todas las distancias (21.04 − 1.29 = 19.75 dB).
        EXPECT_NEAR((total4000 - total500), 19.75f, 0.05) << "r=" << r;
    }
    // SHADOW no depende de la distancia: mismo valor en r=0.15 y r=2.0.
    EXPECT_NEAR(ivanna::psi::shadowDb(th, 1000.0f), 4.31f, 0.05);
    EXPECT_NEAR(ivanna::psi::shadowDb(th, 500.0f), 1.29f, 0.05);
    EXPECT_NEAR(ivanna::psi::shadowDb(th, 4000.0f), 21.04f, 0.05);
}

// ── Ec. 2: radio de cabeza personalizado mueve el ILD ───────────────────────
TEST(PsiField, PersonalizedHeadRadiusMovesIld) {
    const float th = 1.5707963f;
    const float smallHead = ivanna::psi::ildDb(0.30f, th, 1000.0f, 0.075f);
    const float refHead = ivanna::psi::ildDb(0.30f, th, 1000.0f, 0.0875f);
    const float bigHead = ivanna::psi::ildDb(0.30f, th, 1000.0f, 0.10f);
    EXPECT_LT(smallHead, refHead);
    EXPECT_LT(refHead, bigHead);
}

// ── Ec. 3: factor 1.0 en referencia; sensible al ancho de cabeza ────────────
TEST(PsiField, PersonalizationFactorReferenceIsOne) {
    CipicAnthropometry ref;
    for (int l = 0; l <= 6; ++l) {
        EXPECT_FLOAT_EQ(ivanna::psi::personalizationFactor(l, 0, ref, ref), 1.0f)
            << "l=" << l;
    }
    // l=0 (monopolo) nunca se personaliza.
    CipicAnthropometry big;
    big.x1_headWidthCm = 25.0f;
    EXPECT_FLOAT_EQ(ivanna::psi::personalizationFactor(0, 0, big, ref), 1.0f);
}

TEST(PsiField, PersonalizationFactorHeadWidthSensitivity) {
    CipicAnthropometry ref;
    CipicAnthropometry big;
    big.x1_headWidthCm = 19.8f;  // +13.1% vs referencia 17.5
    // l=1: 1 + 0.5 · (0.5 · 0.1314) = 1.0329
    const float f1 = ivanna::psi::personalizationFactor(1, 0, big, ref);
    EXPECT_NEAR(f1, 1.033f, 0.005f);
    EXPECT_GT(f1, 1.0f);
    // Cabeza más chica → factor < 1.
    CipicAnthropometry small;
    small.x1_headWidthCm = 15.8f;
    EXPECT_LT(ivanna::psi::personalizationFactor(1, 0, small, ref), 1.0f);
}

TEST(PsiField, PersonalizationWeightsSumToOne) {
    for (int l = 1; l <= 5; ++l) {
        double sum = 0.0;
        for (int j = 0; j < 17; ++j) sum += ivanna::psi::personalizationWeight(l, j);
        EXPECT_NEAR(sum, 1.0, 1e-6) << "l=" << l;
    }
}

TEST(PsiField, PersonalizationFactorClamped) {
    CipicAnthropometry ref;
    CipicAnthropometry extreme;
    extreme.x1_headWidthCm = 100.0f;  // absurdo: debe clampar a 2.0
    EXPECT_LE(ivanna::psi::personalizationFactor(1, 0, extreme, ref), 2.0f);
    EXPECT_GE(ivanna::psi::personalizationFactor(1, 0, extreme, ref), 0.5f);
    extreme.x1_headWidthCm = 1.0f;
    extreme.x2_headHeightCm = 1.0f;
    extreme.x3_headDepthCm = 1.0f;
    EXPECT_GE(ivanna::psi::personalizationFactor(1, 0, extreme, ref), 0.5f);
    EXPECT_LE(ivanna::psi::personalizationFactor(1, 0, extreme, ref), 2.0f);
    // Entrada no finita → factor finito.
    extreme.x1_headWidthCm = NAN;
    const float f = ivanna::psi::personalizationFactor(1, 0, extreme, ref);
    EXPECT_TRUE(std::isfinite(f));
}

TEST(PsiField, HeadRadiusFromAnthropometry) {
    CipicAnthropometry ref;
    // x1 = 17.5 cm (KEMAR) → radio 8.75 cm, exacto al modelo existente.
    EXPECT_NEAR(ivanna::psi::headRadiusFromAnthropometry(ref), 0.0875f, 1e-6f);
    CipicAnthropometry big;
    big.x1_headWidthCm = 20.0f;
    EXPECT_NEAR(ivanna::psi::headRadiusFromAnthropometry(big), 0.10f, 1e-6f);
}

// ── Ec. 4: Γ(η) ─────────────────────────────────────────────────────────────
TEST(PsiField, PsiGamma) {
    EXPECT_FLOAT_EQ(ivanna::psi::psiGamma(0.5f, 0.5f), 1.0f);
    EXPECT_NEAR(ivanna::psi::psiGamma(0.9f, 0.5f, 0.5f), 1.2f, 1e-6f);
    EXPECT_NEAR(ivanna::psi::psiGamma(0.1f, 0.5f, 0.5f), 0.8f, 1e-6f);
    EXPECT_FLOAT_EQ(ivanna::psi::psiGamma(1.0f, 0.0f, 5.0f), 1.5f);  // clamp
    EXPECT_FLOAT_EQ(ivanna::psi::psiGamma(0.0f, 1.0f, 5.0f), 0.5f);  // clamp
    EXPECT_FLOAT_EQ(ivanna::psi::psiGamma(NAN, 0.5f), 1.0f);         // fail-closed
}

// ── Ec. 4 (EDO): con L=0 idéntica al legacy; con L>0 η converge más bajo ───
TEST(PsiField, BioControlLossZeroIsIdenticalToLegacy) {
    Ivanna::BioControl5D a(0.5, 2.0);
    Ivanna::BioControl5D b(0.5, 2.0);
    a.setSensorPresent(true);
    b.setSensorPresent(true);
    for (int i = 0; i < 1000; ++i) {
        a.update(0.01, 0.9);
        b.update(0.01, 0.9, 0.0, 0.5);  // L=0, λ=0.5 → idéntico
        EXPECT_DOUBLE_EQ(a.eta(), b.eta());
    }
}

TEST(PsiField, BioControlLossLowersEquilibrium) {
    // η* = 0.9, τ = 2 s, L = 1.0, λ = 0.1 → equilibrio 0.9 − 0.1·1·2 = 0.7.
    Ivanna::BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    for (int i = 0; i < 2000; ++i) {  // 20 s = 10τ: convergido
        c.update(0.01, 0.9, 1.0, 0.1);
    }
    EXPECT_NEAR(c.eta(), 0.7, 0.02);
    // Sin pérdida, el mismo controlador llega a 0.9.
    Ivanna::BioControl5D d(0.5, 2.0);
    d.setSensorPresent(true);
    for (int i = 0; i < 2000; ++i) d.update(0.01, 0.9);
    EXPECT_NEAR(d.eta(), 0.9, 0.01);
    EXPECT_LT(c.eta(), d.eta());
}

TEST(PsiField, BioControlLossClampsAndNeverEscapes) {
    Ivanna::BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    // Pérdida absurda y λ absurda: η se queda en [0,1], sin NaN.
    for (int i = 0; i < 500; ++i) c.update(0.01, 0.9, 1e6, 1e6);
    EXPECT_TRUE(std::isfinite(c.eta()));
    EXPECT_GE(c.eta(), 0.0);
    EXPECT_LE(c.eta(), 1.0);
}

// ── PsiFieldRuntime: el campo Ψ como objeto (nueva creación) ─────────────────
TEST(PsiFieldRuntime, DefaultMatchesGeometricModel) {
    // Sin antropometría (KEMAR): el runtime coincide con el modelo
    // existente — la personalización es opt-in, no un cambio de sonido.
    ivanna::psi::PsiFieldRuntime rt;
    EXPECT_NEAR(rt.headRadiusM(), 0.0875f, 1e-6f);
    const float rs[] = {0.15f, 0.30f, 0.50f, 1.0f, 2.0f};
    const float thetas[] = {-1.5708f, -0.7854f, 0.0f, 0.7854f, 1.5708f};
    const float freqs[] = {100.0f, 500.0f, 1000.0f, 4000.0f};
    for (float r : rs) {
        for (float th : thetas) {
            for (float f : freqs) {
                EXPECT_NEAR(rt.ildDb(r, th, f),
                            ivanna::nearfield::ildDb(th, r, f), 0.01)
                    << "r=" << r << " th=" << th << " f=" << f;
            }
        }
    }
}

TEST(PsiFieldRuntime, PersonalizedAnthropometryMovesIld) {
    ivanna::psi::PsiFieldRuntime rt;
    CipicAnthropometry big;
    big.x1_headWidthCm = 20.0f;  // cabeza más grande → más ILD
    rt.setAnthropometry(big);
    EXPECT_NEAR(rt.headRadiusM(), 0.10f, 1e-6f);
    const float th = 1.5707963f;
    EXPECT_GT(rt.ildDb(0.30f, th, 1000.0f),
              ivanna::nearfield::ildDb(th, 0.30f, 1000.0f));
    // El factor de personalización del runtime es el del operador.
    EXPECT_NEAR(rt.personalizationFactor(1, 0),
                ivanna::psi::personalizationFactor(1, 0, big), 1e-6f);
}

TEST(PsiFieldRuntime, GammaIsOneWithoutSensor) {
    ivanna::psi::PsiFieldRuntime rt;
    EXPECT_FLOAT_EQ(rt.gamma(), 1.0f);
    EXPECT_DOUBLE_EQ(rt.orbeRadius(0.35, 0.5, 0.01), 0.35);
    EXPECT_DOUBLE_EQ(rt.eta(), 0.5);
}

TEST(PsiFieldRuntime, EtaTracksWithSensorAndLoss) {
    ivanna::psi::PsiFieldRuntime rt;
    rt.setSensorPresent(true);
    for (int i = 0; i < 2000; ++i) rt.update(0.01, 0.9, 1.0, 0.1);
    // Equilibrio 0.9 − 0.1·1·2 = 0.7 (misma EDO que BioControl5D).
    EXPECT_NEAR(rt.eta(), 0.7, 0.02);
    // η por encima de la basal → Γ > 1.
    EXPECT_GT(rt.gamma(), 1.0f);
}

}  // namespace
