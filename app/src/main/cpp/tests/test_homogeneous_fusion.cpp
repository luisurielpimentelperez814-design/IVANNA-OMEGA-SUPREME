// =====================================================================
//  Tests host de HomogeneousFusion (v2.5.367).
//  Verifican: un solo estado Φ (neuro + ruta AUX + puerta Volterra),
//  Z_emf siempre identidad, fail-closed sin frames / apagado, y que
//  el update() por bloque aplica la ganancia neuro en muestras y
//  entrega los objetivos HOLOS/ORBE ajustados sin tocar la base.
// =====================================================================
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "../spatial/HomogeneousFusion.hpp"

using ivanna::neuromorphic::ClosedLoopTargets;
using ivanna::neuromorphic::NeuroFrame;
using ivanna::neuromorphic::NeuroSensorKind;
using ivanna::neuromorphic::kNeuroBands;
using ivanna::neuromorphic::monotonicNowUs;
using ivanna::spatial::AuxPresetKind;
using ivanna::spatial::EmfVector;
using ivanna::spatial::FusionStatus;
using ivanna::spatial::HomogeneousFusion;
using ivanna::dsp::VolterraLoadStatus;
namespace ivk1 = ivanna::dsp::ivk1;

static NeuroFrame strongFrame(std::uint64_t nowUs) {
    NeuroFrame f{};
    f.kind = NeuroSensorKind::EegInEar;
    f.timestampUs = nowUs;
    f.quality = 0.9f;
    for (int b = 0; b < kNeuroBands; ++b) {
        f.response[b] = 0.30f;    // por debajo del objetivo 0.65 -> sube ganancia
        f.coherence[b] = 0.9f;
    }
    return f;
}

static std::vector<std::uint8_t> buildPkg(std::uint32_t k, const char* dataset,
                                          const char* device) {
    const std::uint64_t tri = (std::uint64_t)k * (k + 1u) / 2u;
    const std::size_t total = ivk1::kHeaderSize + 4ull * k + 4ull * tri;
    std::vector<std::uint8_t> b(total, 0);
    std::memcpy(b.data() + ivk1::kOffMagic, ivk1::kMagic, 4);
    b[ivk1::kOffVersion] = 1;
    b[ivk1::kOffKernelLen] = static_cast<std::uint8_t>(k & 0xFF);
    b[ivk1::kOffKernelLen + 1] = static_cast<std::uint8_t>((k >> 8) & 0xFF);
    const std::uint32_t fs = 48000;
    std::memcpy(b.data() + ivk1::kOffSampleRate, &fs, 4);
    std::memcpy(b.data() + ivk1::kOffDatasetId, dataset, std::strlen(dataset));
    std::memcpy(b.data() + ivk1::kOffDeviceId, device, std::strlen(device));
    const float h1 = 0.5f;
    std::memcpy(b.data() + ivk1::kHeaderSize, &h1, 4);
    const std::uint32_t crc = ivanna::dsp::volterraCrc32(
        b.data() + ivk1::kOffDeviceId, total - ivk1::kOffDeviceId);
    std::memcpy(b.data() + ivk1::kOffCrc, &crc, 4);
    return b;
}

TEST(HomogeneousFusion, ApagadoEsIdentidadTotal) {
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    ClosedLoopTargets base{}, out{};
    float L[128], R[128];
    for (int i = 0; i < 128; ++i) { L[i] = 0.5f; R[i] = -0.5f; }
    const FusionStatus st = f.update(L, R, 128, base, out);
    EXPECT_FALSE(st.enabled);
    EXPECT_FALSE(st.neuroActive);
    EXPECT_FALSE(st.volterraReady);
    EXPECT_FALSE(st.emfAvailable);
    EXPECT_FLOAT_EQ(out.holosGamma, base.holosGamma);
    EXPECT_FLOAT_EQ(out.orbeDistanceM, base.orbeDistanceM);
    for (int i = 0; i < 128; ++i) {
        EXPECT_FLOAT_EQ(L[i], 0.5f);  // bit-exacto: no se toca el buffer
        EXPECT_FLOAT_EQ(R[i], -0.5f);
    }
    EXPECT_FALSE(f.ingestNeuro(strongFrame(monotonicNowUs())));
}

TEST(HomogeneousFusion, SinFramesElBucleEsIdentidad) {
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    f.setEnabled(true);
    ClosedLoopTargets base{}, out{};
    float L[128], R[128];
    for (int i = 0; i < 128; ++i) { L[i] = 0.25f; R[i] = 0.25f; }
    const FusionStatus st = f.update(L, R, 128, base, out);
    EXPECT_TRUE(st.enabled);
    EXPECT_FALSE(st.neuroActive);
    EXPECT_FLOAT_EQ(out.holosGamma, base.holosGamma);
    for (int i = 0; i < 128; ++i) EXPECT_FLOAT_EQ(L[i], 0.25f);
}

TEST(HomogeneousFusion, FrameRealActivaElBucleYModulaLaMuestra) {
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    f.setEnabled(true);
    const std::uint64_t now = monotonicNowUs();
    // Varios frames para que la confianza supere 0.30 y el wet suba.
    for (int i = 0; i < 40; ++i) {
        ASSERT_TRUE(f.ingestNeuro(strongFrame(now + i * 10000)));
    }
    ClosedLoopTargets base{}, out{};
    float L[128], R[128];
    for (int i = 0; i < 128; ++i) { L[i] = 0.5f; R[i] = 0.5f; }
    FusionStatus st{};
    for (int i = 0; i < 30; ++i) {
        st = f.update(L, R, 128, base, out, now + 400000 + i * 2667);
    }
    EXPECT_TRUE(st.neuroActive);
    EXPECT_GT(st.neuroWet, 0.0f);
    // La ganancia por banda se aplicó en muestras (no identidad).
    bool changed = false;
    for (int i = 0; i < 128; ++i) {
        if (std::fabs(L[i] - 0.5f) > 1e-6f) { changed = true; break; }
    }
    EXPECT_TRUE(changed);
    // La base no se reescribe: out es una copia ajustada.
    EXPECT_FLOAT_EQ(base.holosGamma, 1.0f);
    EXPECT_FLOAT_EQ(base.orbeDistanceM, 0.35f);
}

TEST(HomogeneousFusion, FrameSimuladoODeBajaCalidadSeRechaza) {
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    f.setEnabled(true);
    NeuroFrame bad = strongFrame(monotonicNowUs());
    bad.kind = NeuroSensorKind::Unknown;
    EXPECT_FALSE(f.ingestNeuro(bad));
    bad = strongFrame(monotonicNowUs());
    bad.quality = 0.1f;
    EXPECT_FALSE(f.ingestNeuro(bad));
}

TEST(HomogeneousFusion, EmfSiempreIdentidad) {
    EXPECT_FALSE(EmfVector::kAvailable);
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    f.setEnabled(true);
    const EmfVector e = f.emfVector();
    EXPECT_FLOAT_EQ(e.microVolts, 0.0f);
    ClosedLoopTargets base{}, out{};
    float L[8] = {0.1f}, R[8] = {0.1f};
    const FusionStatus st = f.update(L, R, 8, base, out);
    EXPECT_FALSE(st.emfAvailable);
}

TEST(HomogeneousFusion, PresetDeRutaEsParteDelEstado) {
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    EXPECT_EQ(f.routePreset(), AuxPresetKind::GeneziRoom);
    EXPECT_FLOAT_EQ(f.routeParams().xtc.spanMeters, 0.90f);
    f.setRoutePreset(AuxPresetKind::CarCabin);
    EXPECT_EQ(f.routePreset(), AuxPresetKind::CarCabin);
    EXPECT_FLOAT_EQ(f.routeParams().xtc.spanMeters, 1.40f);
    EXPECT_FLOAT_EQ(f.routeParams().xtc.listenerOffsetM, -0.30f);
    f.setEnabled(true);
    ClosedLoopTargets base{}, out{};
    float L[8] = {0.1f}, R[8] = {0.1f};
    const FusionStatus st = f.update(L, R, 8, base, out);
    EXPECT_TRUE(st.auxActive);
    EXPECT_EQ(st.routePreset, AuxPresetKind::CarCabin);
}

TEST(HomogeneousFusion, PuertaVolterraGuardaSoloKernelMedido) {
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    auto pub = buildPkg(64, "DATASET0", "MOTO_G85_GENEZY");
    EXPECT_EQ(f.loadVolterraPackage(pub.data(), pub.size()),
              VolterraLoadStatus::ForeignDatasetRejected);
    EXPECT_FALSE(f.volterraReady());
    EXPECT_EQ(f.volterraPackage(), nullptr);
    auto good = buildPkg(64, "GENEZY01", "MOTO_G85_GENEZY");
    EXPECT_EQ(f.loadVolterraPackage(good.data(), good.size()),
              VolterraLoadStatus::Ok);
    EXPECT_TRUE(f.volterraReady());
    ASSERT_NE(f.volterraPackage(), nullptr);
    EXPECT_EQ(f.volterraPackageSize(), good.size());
    EXPECT_EQ(std::memcmp(f.volterraPackage(), good.data(), good.size()), 0);
    // Un rechazo posterior limpia lo guardado (fail-closed).
    EXPECT_EQ(f.loadVolterraPackage(pub.data(), pub.size()),
              VolterraLoadStatus::ForeignDatasetRejected);
    EXPECT_FALSE(f.volterraReady());
    EXPECT_EQ(f.volterraPackage(), nullptr);
}
