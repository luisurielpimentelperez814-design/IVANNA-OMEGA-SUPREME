// test_magistral_room_xtc.cpp — perfil de sala MAGISTRAL y su geometría de transaural (XTC).
//
// Qué fija: (1) las coordenadas dadas por el propietario (sala 3.83×2.83×7.00 m, FL/FR a 1.50 m y 2.70 m de
// separación, SL/SR en paredes laterales a 3.50 m y 1.80 m, subwoofer en el suelo); (2) que la geometría del
// cancelador de diafonía se DERIVA de esas coordenadas en vez de usar la de escritorio (0.90 m / 0.85 m);
// (3) que con esa geometría el cancelador sí reduce la fuga contralateral contra la acústica del modelo.
//
// Qué NO prueba: el sonido real en la sala. La geometría del oyente (eje central, 3.50 m, oídos a 1.20 m) es un
// supuesto y el modelo de cabeza es el del propio módulo; falta medición binaural en el dispositivo.
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <vector>

#include "CrosstalkCanceller.hpp"
#include "RoomGeometryConfig.hpp"
#include "omega_control_bus.h"

using ivanna::spatial::CrosstalkCanceller;
using ivanna::spatial::RoomGeometryConfig;

namespace {
constexpr float kSr = 48000.0f;
enum { FL, FR, SL, SR, TL, TR, SW };

std::vector<float> sine(float f, int n, float a) {
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i) v[(size_t)i] = a * std::sin(2.0f * 3.14159265f * f * (float)i / kSr);
    return v;
}

CrosstalkCanceller::Geometry magistralGeometry() {
    const auto& r = RoomGeometryConfig::defaultLayout();
    const auto p = RoomGeometryConfig::crosstalkPlacement(r.speakerX.data(), r.speakerY.data(), r.speakerZ.data(),
                                                          r.listenerX, r.listenerY, r.listenerZ);
    CrosstalkCanceller::Geometry g;
    g.spanMeters = p.spanM; g.distanceMeters = p.distanceM; g.listenerOffsetM = p.listenerOffsetM;
    return g;
}

// Fuga al oído contralateral (dB) con un tono solo en L. La acústica es la del modelo con la geometría `phys`
// (tope alto para no recortar ρ); el cancelador bajo prueba usa la geometría `used`.
double leakDb(float f, bool engage, const CrosstalkCanceller::Geometry& used, const CrosstalkCanceller::Geometry& phys) {
    CrosstalkCanceller p; p.prepare(kSr, 256); p.setGeometry(phys); p.setMaxMonoBoostDb(9.0f);
    CrosstalkCanceller x; x.prepare(kSr, 256); x.setGeometry(used); x.setEngage(engage);
    const float rho = p.rho();
    const float k = p.itdMicroseconds() * 1.0e-6f * kSr;
    const int N = 48000;
    std::vector<float> L = sine(f, N, 0.25f), R((size_t)N, 0.0f);
    for (int i = 0; i < N; i += 256) x.processBlock(&L[(size_t)i], &R[(size_t)i], std::min(256, N - i));
    auto delayed = [&](const std::vector<float>& v, int n) {
        const int b = (int)std::floor(k); const float fr = k - (float)b;
        const int i0 = n - b, i1 = i0 - 1;
        return (i0 >= 0 ? v[(size_t)i0] : 0.0f) * (1.0f - fr) + (i1 >= 0 ? v[(size_t)i1] : 0.0f) * fr;
    };
    double eL = 0, eR = 0;
    for (int n = 24000; n < N; ++n) {
        const float earL = L[(size_t)n] + rho * delayed(R, n);
        const float earR = R[(size_t)n] + rho * delayed(L, n);
        eL += (double)earL * earL; eR += (double)earR * earR;
    }
    return 10.0 * std::log10((eR + 1e-20) / (eL + 1e-20));
}
} // namespace

TEST(MagistralRoom, LayoutMatchesOwnerCoordinates) {
    const auto& r = RoomGeometryConfig::defaultLayout();
    EXPECT_FLOAT_EQ(r.roomWidthM, 3.83f);
    EXPECT_FLOAT_EQ(r.roomHeightM, 2.83f);
    EXPECT_FLOAT_EQ(r.roomDepthM, 7.0f);
    EXPECT_FLOAT_EQ(r.speakerY[FL], 1.50f);
    EXPECT_FLOAT_EQ(r.speakerY[FR], 1.50f);
    EXPECT_NEAR(r.speakerX[FR] - r.speakerX[FL], 2.70f, 1e-5f);      // separación FL–FR
    EXPECT_NEAR(0.5f * (r.speakerX[FL] + r.speakerX[FR]), r.listenerX, 1e-5f);   // par centrado en el eje
    EXPECT_FLOAT_EQ(r.speakerX[SL], 0.0f);                           // paredes laterales
    EXPECT_FLOAT_EQ(r.speakerX[SR], r.roomWidthM);
    EXPECT_FLOAT_EQ(r.speakerZ[SL], 3.50f);
    EXPECT_FLOAT_EQ(r.speakerZ[SR], 3.50f);
    EXPECT_FLOAT_EQ(r.speakerY[SL], 1.80f);
    EXPECT_FLOAT_EQ(r.speakerY[SR], 1.80f);
    EXPECT_FLOAT_EQ(r.speakerY[SW], 0.0f);                           // a ras de suelo
}

TEST(MagistralRoom, EverySpeakerIsInsideTheRoom) {
    const auto& r = RoomGeometryConfig::defaultLayout();
    for (int i = 0; i < RoomGeometryConfig::kNumSpeakers; ++i) {
        const size_t k = (size_t)i;
        EXPECT_GE(r.speakerX[k], 0.0f); EXPECT_LE(r.speakerX[k], r.roomWidthM);
        EXPECT_GE(r.speakerY[k], 0.0f); EXPECT_LE(r.speakerY[k], r.roomHeightM);   // antes TL/TR a 3.00 m > techo
        EXPECT_GE(r.speakerZ[k], 0.0f); EXPECT_LE(r.speakerZ[k], r.roomDepthM);
    }
    EXPECT_LE(r.listenerY, r.roomHeightM);
}

TEST(MagistralRoom, SnapshotDefaultsCarryTheSameLayout) {
    const auto s = ivanna::OmegaDspSnapshot::makeDefault();
    const auto& r = RoomGeometryConfig::defaultLayout();
    for (int i = 0; i < ivanna::OMEGA_CTRL_WFS_SPEAKERS; ++i) {
        EXPECT_FLOAT_EQ(s.wfs_speaker_x[i], r.speakerX[(size_t)i]) << i;
        EXPECT_FLOAT_EQ(s.wfs_speaker_y[i], r.speakerY[(size_t)i]) << i;
        EXPECT_FLOAT_EQ(s.wfs_speaker_z[i], r.speakerZ[(size_t)i]) << i;
    }
}

TEST(MagistralXtc, PlacementIsDerivedFromTheRoomNotTheDesktopDefault) {
    const auto g = magistralGeometry();
    EXPECT_NEAR(g.spanMeters, 2.70f, 1e-4f);
    EXPECT_NEAR(g.distanceMeters, std::hypot(3.50f, 0.30f), 1e-4f);   // 3.50 m al frente, altavoz 0.30 m sobre el oído
    EXPECT_NEAR(g.listenerOffsetM, 0.0f, 1e-4f);
    const CrosstalkCanceller::Geometry desktop{};
    EXPECT_GT(g.spanMeters, 2.5f * desktop.spanMeters);               // la geometría por defecto estaba 3× mal
    EXPECT_GT(g.distanceMeters, 3.5f * desktop.distanceMeters);
}

TEST(MagistralXtc, ItdAgreesWithWoodworthForTheRoomAngle) {
    // ITD de Woodworth (a/c)(θ+sinθ) con θ = semiángulo del par visto desde el oyente. Independiente del código.
    const auto g = magistralGeometry();
    const float theta = std::atan2(g.spanMeters * 0.5f, g.distanceMeters);
    const float woodworthUs = (g.headRadiusM / g.speedOfSound) * (theta + std::sin(theta)) * 1.0e6f;
    CrosstalkCanceller x; x.prepare(kSr, 256); x.setGeometry(g);
    EXPECT_NEAR(x.itdMicroseconds(), woodworthUs, 0.12f * woodworthUs);
    EXPECT_GT(x.itdMicroseconds(), 150.0f);
    EXPECT_LT(x.itdMicroseconds(), 220.0f);
}

TEST(MagistralXtc, InvalidPlacementIsRejected) {
    const auto& r = RoomGeometryConfig::defaultLayout();
    float x[7], y[7], z[7];
    auto reset = [&] { for (int i = 0; i < 7; ++i) { x[i] = r.speakerX[(size_t)i]; y[i] = r.speakerY[(size_t)i]; z[i] = r.speakerZ[(size_t)i]; } };
    reset();
    EXPECT_TRUE(RoomGeometryConfig::crosstalkPlacement(x, y, z, r.listenerX, r.listenerY, r.listenerZ).valid);
    reset(); x[FL] = std::nanf("");
    EXPECT_FALSE(RoomGeometryConfig::crosstalkPlacement(x, y, z, r.listenerX, r.listenerY, r.listenerZ).valid);
    reset(); x[FL] = x[FR];                                   // separación 0
    EXPECT_FALSE(RoomGeometryConfig::crosstalkPlacement(x, y, z, r.listenerX, r.listenerY, r.listenerZ).valid);
    reset();                                                  // oyente sobre el plano de los altavoces (z = 0, a su altura)
    EXPECT_FALSE(RoomGeometryConfig::crosstalkPlacement(x, y, z, r.listenerX, 1.50f, 0.0f).valid);
    EXPECT_FALSE(RoomGeometryConfig::crosstalkPlacement(nullptr, y, z, 1, 1, 1).valid);
}

TEST(MagistralXtc, OffCenterListenerShiftsTheItdAndRelaxesRho) {
    const auto& r = RoomGeometryConfig::defaultLayout();
    const auto c = RoomGeometryConfig::crosstalkPlacement(r.speakerX.data(), r.speakerY.data(), r.speakerZ.data(), 1.915f, 1.20f, 3.50f);
    const auto o = RoomGeometryConfig::crosstalkPlacement(r.speakerX.data(), r.speakerY.data(), r.speakerZ.data(), 2.40f, 1.20f, 3.50f);
    EXPECT_NEAR(o.listenerOffsetM, 0.485f, 1e-4f);
    CrosstalkCanceller a, b;
    a.prepare(kSr, 256); b.prepare(kSr, 256);
    auto ga = magistralGeometry(); ga.listenerOffsetM = c.listenerOffsetM; a.setGeometry(ga);
    auto gb = magistralGeometry(); gb.listenerOffsetM = o.listenerOffsetM; b.setGeometry(gb);
    EXPECT_NE(a.itdMicroseconds(), b.itdMicroseconds());
    EXPECT_LE(b.rho(), a.rho() + 1e-6f);                      // fuera del punto dulce la cancelación se relaja
    EXPECT_TRUE(std::isfinite(b.rho()));
}

TEST(MagistralXtc, CancelsContralateralLeakInTheRoomGeometry) {
    const auto g = magistralGeometry();
    for (float f : {500.0f, 1000.0f, 2000.0f, 4000.0f}) {
        const double off = leakDb(f, false, g, g);
        const double on  = leakDb(f, true,  g, g);
        EXPECT_LT(on, off - 5.0) << "f=" << f << " off=" << off << " on=" << on;
    }
}

TEST(MagistralXtc, RoomGeometryBeatsTheDesktopDefaultOnTheRoomAcoustics) {
    // La acústica es la de la sala; un cancelador con la geometría de escritorio (ITD 238 µs frente a 183 µs: ≈ 2.6 muestras de más)
    // desajusta la fase del cruce y cancela peor. Es la razón de conectar la geometría a la ruta real.
    const auto g = magistralGeometry();
    const CrosstalkCanceller::Geometry desktop{};
    double matched = 0, mismatched = 0;
    for (float f : {1000.0f, 2000.0f, 3000.0f}) {
        matched    += leakDb(f, true, g, g);
        mismatched += leakDb(f, true, desktop, g);
    }
    EXPECT_LT(matched, mismatched - 3.0) << "matched=" << matched << " mismatched=" << mismatched;
}

TEST(MagistralXtc, CenteredContentBoostStaysWithinTheCap) {
    const auto g = magistralGeometry();
    CrosstalkCanceller x; x.prepare(kSr, 256); x.setGeometry(g); x.setEngage(true);
    EXPECT_LE(x.worstMonoBoostDb(), x.maxMonoBoostDb() + 1e-3f);
    EXPECT_LT(x.dcGainDb(), 6.0f);                              // mismo criterio que la prueba del módulo
    for (float f : {40.0f, 2700.0f}) {
        CrosstalkCanceller y; y.prepare(kSr, 256); y.setGeometry(g); y.setEngage(true);
        const int N = 48000;
        std::vector<float> L = sine(f, N, 0.25f), R = L;
        for (int i = 0; i < N; i += 256) y.processBlock(&L[(size_t)i], &R[(size_t)i], std::min(256, N - i));
        double pk = 0;
        for (int i = 24000; i < N; ++i) pk = std::max(pk, (double)std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
        const double gainDb = 20.0 * std::log10(pk / 0.25);
        EXPECT_LE(gainDb, 4.0 + 0.6) << f;
        if (f < 100.0f) { EXPECT_NEAR(gainDb, 0.0, 1.2) << "el sub no debe tocarse (cabeza transparente < 120 Hz)"; }
    }
}
