// =====================================================================
//  Tests host de AuxGeneziRoute (v2.5.366).
//  Verifican: presets con geometrías declaradas, la puerta Volterra
//  (acepta solo GENEZY01/K=64/MOTO_G85_GENEZY, rechaza lo demás), y
//  que K=4096 es inviable en tiempo real (coste O(K^2) en código,
//  no solo en el doc).
// =====================================================================
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "../spatial/AuxGeneziRoute.hpp"

using ivanna::spatial::AuxPresetKind;
using ivanna::spatial::AuxRouteParams;
using ivanna::spatial::AuxVolterraGate;
using ivanna::spatial::AuxSensingLimits;
using ivanna::spatial::auxPreset;
using ivanna::spatial::kAuxVolterraKernelLength;
using ivanna::spatial::volterraH2Cost;
using ivanna::dsp::VolterraLoadStatus;
namespace ivk1 = ivanna::dsp::ivk1;

// Construye un paquete IVK1 válido (K, dataset, device, h1/h2) con CRC
// correcto. h1[0]=0.5, resto 0; h2=0 -> pasa los presupuestos.
static std::vector<std::uint8_t> buildPkg(std::uint32_t k, const char* dataset,
                                          const char* device) {
    const std::uint64_t tri = (std::uint64_t)k * (k + 1u) / 2u;
    const std::size_t total = ivk1::kHeaderSize + 4ull * k + 4ull * tri;
    std::vector<std::uint8_t> b(total, 0);
    std::memcpy(b.data() + ivk1::kOffMagic, ivk1::kMagic, 4);
    b[ivk1::kOffVersion] = 1;
    b[ivk1::kOffVersion + 1] = 0;
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

TEST(AuxGeneziRoute, PresetsTienenGeometriasDistintasYAcotadas) {
    const AuxRouteParams room = auxPreset(AuxPresetKind::GeneziRoom);
    const AuxRouteParams car = auxPreset(AuxPresetKind::CarCabin);
    EXPECT_FLOAT_EQ(room.xtc.spanMeters, 0.90f);
    EXPECT_FLOAT_EQ(room.xtc.distanceMeters, 0.85f);
    EXPECT_FLOAT_EQ(room.xtc.listenerOffsetM, 0.0f);
    EXPECT_FLOAT_EQ(room.auxXoverHz, 120.0f);
    EXPECT_FLOAT_EQ(car.xtc.spanMeters, 1.40f);
    EXPECT_FLOAT_EQ(car.xtc.distanceMeters, 0.80f);
    EXPECT_FLOAT_EQ(car.xtc.listenerOffsetM, -0.30f);
    EXPECT_FLOAT_EQ(car.auxXoverHz, 100.0f);
    // Sin hrtfWetDry en el preset: Ruta B fuerza el wet HRTF a 0 con AUX
    // activo (binaural = audífonos). La elevación por altavoces es un
    // módulo futuro (pinna-EQ), no un valor que la cadena ignoraría.
    EXPECT_GE(room.auxCoherence, 0.0f);
    EXPECT_GT(car.xtc.spanMeters, room.xtc.spanMeters);
    for (const auto& p : {room, car}) {
        EXPECT_TRUE(p.auxFieldEnabled);
        EXPECT_GE(p.auxXoverHz, 40.0f);
        EXPECT_LE(p.auxXoverHz, 250.0f);
        EXPECT_GE(p.auxCoherence, 0.0f);
        EXPECT_LE(p.auxCoherence, 1.0f);
    }
}

TEST(AuxGeneziRoute, K4096EsInviableEnTiempoReal) {
    const auto c64 = volterraH2Cost(64);
    EXPECT_EQ(c64.h2Coeffs, 2080u);
    EXPECT_LT(c64.gmacPerSecondStereo48k, 1.0);  // ~0.4 GMAC/s: viable
    const auto c4096 = volterraH2Cost(4096);
    EXPECT_EQ(c4096.h2Coeffs, 8390656u);
    EXPECT_EQ(c4096.h2Bytes, 33562624u);  // 33 MB solo H2
    EXPECT_GT(c4096.gmacPerSecondStereo48k, 500.0);  // ~806 GMAC/s
    EXPECT_GT(c4096.macsPerSample, c64.macsPerSample * 1000u);
    EXPECT_EQ(kAuxVolterraKernelLength, 64u);
}

TEST(AuxGeneziRoute, LimitesDeSensadoSonLosDeclarados) {
    EXPECT_FALSE(AuxSensingLimits::kBackEmfViaAux);
    EXPECT_TRUE(AuxSensingLimits::kMicCalibration);
    EXPECT_FALSE(AuxSensingLimits::kDsdDirectViaAux);
    EXPECT_FALSE(AuxSensingLimits::kWallReflectionCancellation);
}

TEST(AuxGeneziRoute, GateAceptaGenezyMedidoK64) {
    auto pkg = buildPkg(64, "GENEZY01", "MOTO_G85_GENEZY");
    EXPECT_EQ(AuxVolterraGate::validate(pkg.data(), pkg.size()),
              VolterraLoadStatus::Ok);
}

TEST(AuxGeneziRoute, GateRechazaDatasetPublicoYSinteticoPorDefecto) {
    auto pub = buildPkg(64, "DATASET0", "MOTO_G85_GENEZY");
    EXPECT_EQ(AuxVolterraGate::validate(pub.data(), pub.size()),
              VolterraLoadStatus::ForeignDatasetRejected);
    auto syn = buildPkg(64, "SYNTH000", "MOTO_G85_GENEZY");
    EXPECT_EQ(AuxVolterraGate::validate(syn.data(), syn.size()),
              VolterraLoadStatus::SyntheticRejected);
    EXPECT_EQ(AuxVolterraGate::validate(syn.data(), syn.size(), true),
              VolterraLoadStatus::Ok);
}

TEST(AuxGeneziRoute, GateRechazaK4096YDispositivoDistinto) {
    auto k4096 = buildPkg(4096, "GENEZY01", "MOTO_G85_GENEZY");
    EXPECT_EQ(AuxVolterraGate::validate(k4096.data(), k4096.size()),
              VolterraLoadStatus::KernelLengthMismatch);
    auto other = buildPkg(64, "GENEZY01", "OTRO_DISPOSITIVO");
    EXPECT_EQ(AuxVolterraGate::validate(other.data(), other.size()),
              VolterraLoadStatus::DeviceMismatch);
    auto trunc = buildPkg(64, "GENEZY01", "MOTO_G85_GENEZY");
    trunc.pop_back();
    EXPECT_EQ(AuxVolterraGate::validate(trunc.data(), trunc.size()),
              VolterraLoadStatus::Truncated);
    EXPECT_EQ(AuxVolterraGate::validate(nullptr, 0),
              VolterraLoadStatus::NullArgument);
}
