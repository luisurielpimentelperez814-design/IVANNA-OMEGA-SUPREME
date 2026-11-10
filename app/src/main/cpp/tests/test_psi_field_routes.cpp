// test_psi_field_routes.cpp — Reformulación por vía de reproducción.
//
// El campo Ψ ya no es solo binaural: la misma ley (SPREAD + SHADOW) se
// percibe distinto por cada vía (audífonos, AUX, bocinas del celular,
// estéreo, 5.1, 7.1, cine) porque cada vía mezcla los canales (crosstalk)
// y reparte las fuentes entre bocinas discretas. Estos tests verifican:
//   · La ley de crosstalk (exacta): κ=0 → ILD intacto; κ=1 → ILD 0.
//   · El ILD percibido por vía: audífonos > cine > estéreo > celular.
//   · Las bocinas de cada vía (azimuts y conteos) y la ley dipolar.
//   · El runtime con vía: por defecto audífonos (sin cambio de sonido).
//   · El modelo de cabeza con radio personalizado (NearFieldHeadModel).
// Los tests end-to-end de ORBE con radio personalizado viven en
// test_orbe_binaural.cpp (ese target compila OrbeBinaural.cpp).

#include <gtest/gtest.h>

#include <cmath>

#include "../spatial/NearFieldHeadModel.hpp"
#include "../spatial/PsiField.hpp"
#include "../spatial/PsiFieldRuntime.hpp"

namespace {

constexpr float kPiF = 3.14159265358979323846f;

}  // namespace

// ── Ley de crosstalk (exacta) ───────────────────────────────────────────────
TEST(PsiRoute, CrosstalkLawEndpoints) {
    using ivanna::psi::crosstalkIldDb;
    EXPECT_FLOAT_EQ(crosstalkIldDb(10.0f, 0.0f), 10.0f);  // audífonos: intacto
    EXPECT_FLOAT_EQ(crosstalkIldDb(10.0f, 1.0f), 0.0f);   // mono: sin ILD
    EXPECT_FLOAT_EQ(crosstalkIldDb(-6.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(crosstalkIldDb(0.0f, 0.5f), 0.0f);
    // κ=0.5 sobre 10 dB: ρ=3.162 → (3.662/2.581) → 3.04 dB.
    EXPECT_NEAR(crosstalkIldDb(10.0f, 0.5f), 3.04f, 0.02f);
    // Simétrica: el signo se conserva.
    EXPECT_NEAR(crosstalkIldDb(-10.0f, 0.5f), -3.04f, 0.02f);
    // No finito → 0, fail-closed.
    EXPECT_FLOAT_EQ(crosstalkIldDb(std::nanf(""), 0.5f), 0.0f);
}

// ── ILD percibido por vía ───────────────────────────────────────────────────
TEST(PsiRoute, HeadphonesIldEqualsFieldIld) {
    using ivanna::psi::ReproductionRoute;
    const float rs[] = {0.15f, 0.30f, 0.50f, 1.0f, 2.0f};
    const float ths[] = {-1.5708f, -0.7854f, 0.7854f, 1.5708f};
    const float fs[] = {100.0f, 500.0f, 1000.0f, 4000.0f};
    for (float r : rs)
        for (float th : ths)
            for (float f : fs)
                EXPECT_NEAR(ivanna::psi::routeIldDb(ReproductionRoute::Headphones, r, th, f),
                            ivanna::psi::ildDb(r, th, f), 0.001f);
}

TEST(PsiRoute, PerceivedIldOrdering) {
    using ivanna::psi::ReproductionRoute;
    // Fuente a 90°, 1 kHz, 0.30 m. κ: audífonos 0 < cine 0.2 < estéreo 0.3
    // < celular 0.7 → el ILD percibido decrece en ese orden.
    const float h = ivanna::psi::routeIldDb(ReproductionRoute::Headphones, 0.30f, 1.5708f, 1000.0f);
    const float c = ivanna::psi::routeIldDb(ReproductionRoute::Cinema, 0.30f, 1.5708f, 1000.0f);
    const float s = ivanna::psi::routeIldDb(ReproductionRoute::StereoSpeakers, 0.30f, 1.5708f, 1000.0f);
    const float p = ivanna::psi::routeIldDb(ReproductionRoute::PhoneSpeakers, 0.30f, 1.5708f, 1000.0f);
    EXPECT_GT(h, 0.0f);
    EXPECT_LT(c, h);
    EXPECT_LT(s, c);
    EXPECT_LT(p, s);
    // El celular, con κ=0.7, deja menos de la mitad del ILD del campo.
    EXPECT_LT(p, 0.5f * h);
}

TEST(PsiRoute, RouteGeometrySanity) {
    using ivanna::psi::ReproductionRoute;
    const auto hp = ivanna::psi::routeGeometry(ReproductionRoute::Headphones);
    EXPECT_TRUE(hp.binaural);
    EXPECT_FLOAT_EQ(hp.crosstalk, 0.0f);
    EXPECT_EQ(hp.channels, 2);
    const auto ph = ivanna::psi::routeGeometry(ReproductionRoute::PhoneSpeakers);
    EXPECT_FALSE(ph.binaural);
    EXPECT_NEAR(ph.baselineM, 0.12f, 1e-6f);
    EXPECT_EQ(ph.channels, 2);
    EXPECT_EQ(ivanna::psi::routeGeometry(ReproductionRoute::Surround51).channels, 6);
    EXPECT_EQ(ivanna::psi::routeGeometry(ReproductionRoute::Surround71).channels, 8);
    EXPECT_EQ(ivanna::psi::routeGeometry(ReproductionRoute::Cinema).channels, 8);
}

// ── Bocinas de cada vía ─────────────────────────────────────────────────────
TEST(PsiRoute, SpeakerAzimuths) {
    using ivanna::psi::ReproductionRoute;
    float az[7];
    EXPECT_EQ(ivanna::psi::routeSpeakerAzimuths(ReproductionRoute::Headphones, az, 7), 0);
    EXPECT_EQ(ivanna::psi::routeSpeakerAzimuths(ReproductionRoute::PhoneSpeakers, az, 7), 0);
    EXPECT_EQ(ivanna::psi::routeSpeakerAzimuths(ReproductionRoute::StereoSpeakers, az, 7), 2);
    EXPECT_NEAR(az[0], -0.5236f, 1e-4f);  // L −30°
    EXPECT_NEAR(az[1], 0.5236f, 1e-4f);   // R +30°
    EXPECT_EQ(ivanna::psi::routeSpeakerAzimuths(ReproductionRoute::Surround51, az, 7), 5);
    EXPECT_NEAR(az[2], 0.0f, 1e-6f);      // C 0°
    EXPECT_NEAR(az[4], 1.9199f, 1e-4f);   // Rs +110°
    EXPECT_EQ(ivanna::psi::routeSpeakerAzimuths(ReproductionRoute::Surround71, az, 7), 7);
    EXPECT_EQ(ivanna::psi::routeSpeakerAzimuths(ReproductionRoute::Cinema, az, 7), 7);
    EXPECT_EQ(ivanna::psi::routeSpeakerAzimuths(ReproductionRoute::StereoSpeakers, nullptr, 7), 0);
}

TEST(PsiRoute, SpeakerGainDipoleLaw) {
    using ivanna::psi::speakerGainLinear;
    using ivanna::psi::speakerGainDb;
    EXPECT_NEAR(speakerGainLinear(0.5236f, 0.5236f), 1.0f, 1e-6f);  // en la bocina
    EXPECT_NEAR(speakerGainLinear(0.5236f + kPiF * 0.5f, 0.5236f), 0.0f, 1e-6f);  // 90°
    EXPECT_NEAR(speakerGainLinear(-0.5236f, 0.5236f), 0.5f, 1e-3f);  // 60° de diferencia: cos 60° = 0.5
    EXPECT_NEAR(speakerGainLinear(-1.5708f, 0.5236f), 0.0f, 1e-6f);  // 120°: fuera del lóbulo
    // Simétrica y suave: a 30° de la bocina, cos(30°) = 0.866.
    EXPECT_NEAR(speakerGainLinear(0.0f, 0.5236f), 0.866f, 1e-3f);
    EXPECT_FLOAT_EQ(speakerGainDb(0.5236f, 0.5236f), 0.0f);
    EXPECT_FLOAT_EQ(speakerGainDb(0.5236f + kPiF * 0.5f, 0.5236f), -120.0f);
}

// ── Runtime con vía ─────────────────────────────────────────────────────────
TEST(PsiRouteRuntime, DefaultRouteIsHeadphones) {
    ivanna::psi::PsiFieldRuntime rt;
    EXPECT_EQ(rt.route(), ivanna::psi::ReproductionRoute::Headphones);
    EXPECT_TRUE(rt.routeGeometry().binaural);
    // Por defecto el ILD percibido es el ILD del campo (sin cambio).
    EXPECT_NEAR(rt.effectiveIldDb(0.30f, 1.5708f, 1000.0f),
                rt.ildDb(0.30f, 1.5708f, 1000.0f), 1e-6f);
}

TEST(PsiRouteRuntime, PhoneRouteReducesPerceivedIld) {
    ivanna::psi::PsiFieldRuntime rt;
    const float hp = rt.effectiveIldDb(0.30f, 1.5708f, 1000.0f);
    rt.setRoute(ivanna::psi::ReproductionRoute::PhoneSpeakers);
    EXPECT_EQ(rt.routeGeometry().channels, 2);
    EXPECT_LT(rt.effectiveIldDb(0.30f, 1.5708f, 1000.0f), 0.5f * hp);
    // La ganancia de bocina del runtime es la del operador.
    EXPECT_NEAR(rt.speakerGainDb(0.0f, 0.5236f), -1.25f, 0.05f);  // 20·log10(0.866)
}

// ── Modelo de cabeza con radio personalizado ────────────────────────────────
TEST(PsiRoute, NearFieldModelRadiusParameter) {
    // Sin radio: idéntico al modelo original (default KEMAR).
    EXPECT_NEAR(ivanna::nearfield::ildDb(1.5708f, 0.30f, 1000.0f),
                ivanna::nearfield::ildDb(1.5708f, 0.30f, 1000.0f, 0.0875f), 1e-6f);
    // Cabeza más grande → más ILD (misma fuente, mismo azimut).
    // r=0.30 m, θ=90°, f=1 kHz: KEMAR 9.53 dB, radio 10 cm 11.31 dB.
    const float kemar = ivanna::nearfield::ildDb(1.5708f, 0.30f, 1000.0f);
    const float big = ivanna::nearfield::ildDb(1.5708f, 0.30f, 1000.0f, 0.10f);
    EXPECT_GT(big, kemar);
    EXPECT_NEAR(big, 11.31f, 0.05f);
    EXPECT_NEAR(kemar, 9.53f, 0.05f);
    // El ITD también crece con el radio.
    EXPECT_GT(ivanna::nearfield::itdSeconds(1.5708f, 0.30f, 0.10f),
              ivanna::nearfield::itdSeconds(1.5708f, 0.30f, 0.0875f));
}
