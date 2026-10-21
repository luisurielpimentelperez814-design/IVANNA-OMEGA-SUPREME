// Pruebas de QualcommPalClockIntegration (hal_integration/) con callbacks FALSOS.
//
// Alcance honesto: esto valida la lógica del seam en host (validación de la
// muestra pareada, fail-closed, publicación opcional por memoria compartida,
// reset). NO valida el HAL real de Motorola/Qualcomm ni ningún ASRC físico: ese
// enlace sigue sin conectar a propósito (ver hal_integration/
// MOTO_G85_PAL_CLOCK_INTEGRATION.md).

#include "include/audio_hardware_clock_bridge.hpp"
#include "../../../../../hal_integration/ivanna_qualcomm_pal_clock_integration.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using ivanna::audio::HardwareClockReader;
using ivanna::audio::HardwareClockSample;
using ivanna::audio::QualcommPalClockIntegration;

std::string testPath(const char* tag) {
    return std::string("/tmp/ivanna_pal_integration_") + tag + "_" + std::to_string(::getpid());
}

struct FakeAsrc {
    std::vector<double> ratios;
    bool accept = true;
    static bool apply(void* p, double ratio) noexcept {
        auto* self = static_cast<FakeAsrc*>(p);
        self->ratios.push_back(ratio);
        return self->accept;
    }
};

// "HAL" falso: cada llamada a query() entrega la muestra apuntada por `sample`.
// Fail-closed del diseño existente: ante una muestra inválida el controlador no
// "deja el ASRC como estaba", lo devuelve a razón unidad (1.0). Lo único que una
// muestra inválida puede provocar es ESO, nunca una corrección distinta de 1.0.
void expectOnlyUnityCommands(const FakeAsrc& asrc) {
    for (double r : asrc.ratios) EXPECT_DOUBLE_EQ(r, 1.0);
}

struct FakePal {
    HardwareClockSample sample{0, 1000000000ull, 48000};
    bool queryOk = true;
    int queries = 0;
    FakeAsrc asrc;
    static bool query(void* p, HardwareClockSample* out) noexcept {
        auto* self = static_cast<FakePal*>(p);
        ++self->queries;
        if (!self->queryOk || !out) return false;
        *out = self->sample;
        return true;
    }
};

// Secuencia que ya bloquea el servo en test_hal_output_rate_control.
void lockServo(QualcommPalClockIntegration& pal, FakePal& hal) {
    hal.sample = {0, 1000000000ull, 48000};      EXPECT_TRUE(pal.controlTick());
    hal.sample = {4800, 1100000000ull, 48000};   EXPECT_TRUE(pal.controlTick());
    hal.sample = {9601, 1200000000ull, 48000};   EXPECT_TRUE(pal.controlTick());
    hal.sample = {14402, 1300000000ull, 48000};  EXPECT_TRUE(pal.controlTick());
}

TEST(QualcommPalClockIntegration, NullQueryFailsClosedToUnityRatio) {
    FakeAsrc asrc;
    QualcommPalClockIntegration pal(nullptr, nullptr, &FakeAsrc::apply, &asrc);
    EXPECT_FALSE(pal.controlTick());
    EXPECT_FALSE(pal.correctionEnabled());
    expectOnlyUnityCommands(asrc);
    if (pal.hasKnownAppliedRatio()) EXPECT_DOUBLE_EQ(pal.lastAppliedRatio(), 1.0);
}

TEST(QualcommPalClockIntegration, RejectsUnpairedOrImplausibleSamplesAndNeverCorrects) {
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);

    hal.sample = {4800, 0, 48000};                 // sin marca de tiempo de hardware
    EXPECT_FALSE(pal.controlTick());
    hal.sample = {4800, 1000000000ull, 7999};      // tasa por debajo del rango
    EXPECT_FALSE(pal.controlTick());
    hal.sample = {4800, 1000000000ull, 768001};    // tasa por encima del rango
    EXPECT_FALSE(pal.controlTick());
    hal.queryOk = false;                           // el driver no pudo responder
    EXPECT_FALSE(pal.controlTick());

    EXPECT_EQ(hal.queries, 4);
    EXPECT_FALSE(pal.correctionEnabled());
    expectOnlyUnityCommands(hal.asrc);             // jamás una razón distinta de 1.0
    if (pal.hasKnownAppliedRatio()) EXPECT_DOUBLE_EQ(pal.lastAppliedRatio(), 1.0);
}

TEST(QualcommPalClockIntegration, AcceptsBoundaryRates) {
    for (uint32_t rate : {8000u, 768000u}) {
        FakePal hal;
        hal.sample = {0, 1000000000ull, rate};
        QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
        EXPECT_TRUE(pal.controlTick()) << "tasa " << rate;
    }
}

TEST(QualcommPalClockIntegration, ValidPairedSamplesLockAndCommandTheExistingAsrc) {
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    lockServo(pal, hal);
    EXPECT_TRUE(pal.correctionEnabled());
    EXPECT_TRUE(pal.hasKnownAppliedRatio());
    EXPECT_NE(pal.lastAppliedRatio(), 1.0);
    EXPECT_GT(hal.asrc.ratios.size(), 3u);
}

TEST(QualcommPalClockIntegration, QueryFailureAfterLockDisablesCorrection) {
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    lockServo(pal, hal);
    ASSERT_TRUE(pal.correctionEnabled());
    hal.queryOk = false;
    EXPECT_FALSE(pal.controlTick());
    EXPECT_FALSE(pal.correctionEnabled());
}

TEST(QualcommPalClockIntegration, AsrcRejectionFailsClosedAndMarksRatioUnknown) {
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    hal.sample = {0, 1000000000ull, 48000};      ASSERT_TRUE(pal.controlTick());
    hal.sample = {4800, 1100000000ull, 48000};   ASSERT_TRUE(pal.controlTick());
    hal.sample = {9601, 1200000000ull, 48000};   ASSERT_TRUE(pal.controlTick());
    hal.asrc.accept = false;
    hal.sample = {14402, 1300000000ull, 48000};
    EXPECT_FALSE(pal.controlTick());
    EXPECT_FALSE(pal.correctionEnabled());
    EXPECT_FALSE(pal.hasKnownAppliedRatio());
}

TEST(QualcommPalClockIntegration, ResetDropsLockSoCorrectionMustBeReEarned) {
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    lockServo(pal, hal);
    ASSERT_TRUE(pal.correctionEnabled());
    pal.reset();                                   // standby / cambio de ruta / discontinuidad
    EXPECT_FALSE(pal.correctionEnabled());
    hal.sample = {20000, 1400000000ull, 48000};
    EXPECT_TRUE(pal.controlTick());
    EXPECT_FALSE(pal.correctionEnabled()) << "tras reset no puede corregir con la primera muestra";
}

TEST(QualcommPalClockIntegration, PublishesTheSameSampleItControlsWith) {
    const std::string path = testPath("pub");
    std::remove(path.c_str());
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    ASSERT_TRUE(pal.openReporter(path.c_str()));
    EXPECT_TRUE(pal.reporterOpen());

    hal.sample = {96000, 3000000000ull, 48000};
    ASSERT_TRUE(pal.controlTick());

    HardwareClockReader reader;
    ASSERT_TRUE(reader.open(path.c_str()));
    HardwareClockSample got{};
    ASSERT_TRUE(reader.read(got));
    EXPECT_EQ(got.presentedFrames, 96000u);
    EXPECT_EQ(got.hardwareTimestampNs, 3000000000ull);
    EXPECT_EQ(got.nominalSampleRateHz, 48000u);
    reader.close();
    std::remove(path.c_str());
}

TEST(QualcommPalClockIntegration, InvalidSampleNeverOverwritesPublishedSnapshot) {
    const std::string path = testPath("keep");
    std::remove(path.c_str());
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    ASSERT_TRUE(pal.openReporter(path.c_str()));

    hal.sample = {48000, 2000000000ull, 48000};
    ASSERT_TRUE(pal.controlTick());
    hal.sample = {99999, 0, 48000};                // inválida: sin timestamp
    EXPECT_FALSE(pal.controlTick());

    HardwareClockReader reader;
    ASSERT_TRUE(reader.open(path.c_str()));
    HardwareClockSample got{};
    ASSERT_TRUE(reader.read(got));
    EXPECT_EQ(got.presentedFrames, 48000u) << "la muestra inválida no debe fabricarse ni publicarse";
    reader.close();
    std::remove(path.c_str());
}

TEST(QualcommPalClockIntegration, ReporterIsOptionalAndItsFailureDoesNotDisableLocalControl) {
    FakePal hal;
    QualcommPalClockIntegration pal(&FakePal::query, &hal, &FakeAsrc::apply, &hal.asrc);
    EXPECT_FALSE(pal.reporterOpen());              // nunca abierto
    lockServo(pal, hal);
    EXPECT_TRUE(pal.correctionEnabled());

    FakePal hal2;
    QualcommPalClockIntegration pal2(&FakePal::query, &hal2, &FakeAsrc::apply, &hal2.asrc);
    EXPECT_FALSE(pal2.openReporter("/nonexistent_dir_ivanna/clock"));   // IPC no disponible
    EXPECT_FALSE(pal2.reporterOpen());
    lockServo(pal2, hal2);
    EXPECT_TRUE(pal2.correctionEnabled()) << "sin IPC el control local debe seguir funcionando";
}

} // namespace
