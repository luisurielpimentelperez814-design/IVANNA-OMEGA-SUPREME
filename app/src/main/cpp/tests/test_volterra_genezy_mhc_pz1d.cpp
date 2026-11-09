// test_volterra_genezy_mhc_pz1d.cpp — Kernel Volterra H1/H2 del Sony Genezi
// MHC-PZ1D (paquete IVK1 del repo, dataset GENEZY01, K=64, fs 48 kHz).
//
// El .ivk1 lo genera tools/volterra_h2/build_genezy_mhc_pz1d_ivk1.py desde
// los coeficientes tabulados del PDF (ESS Farina) y vive en
// app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1. La ruta llega por la
// definición IVANNA_GENEZY_IVK1_PATH (tests/CMakeLists.txt); si no está
// definida se prueba una ruta relativa al árbol del repo.
//
// Qué verifica (en orden de la puerta -> motor):
//   (a) el .ivk1 pasa AuxVolterraGate (política de producción) — puerta C++.
//   (b) HomogeneousFusion::loadVolterraPackage lo guarda (vía de la fusión).
//   (c) VolterraH2Symmetric::loadKernelPackage lo aplica (sin habilitar).
//   (d) el motor procesa: seno finito y acotado, distinto de la identidad
//       de forma acotada, y la salida coincide con una referencia escalar
//       del funcional de Volterra con los coeficientes del paquete.
//   (e) bypass bit-exacto sin kernel y tras desactivar (drain a silencio).
//
// El kernel NO se activa por defecto: cargar no habilita el motor
// (loadKernelPackage deja isEnabled()==false); hay que habilitarlo
// explícitamente con setEnabled(true). Fail-closed ante cualquier error.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../neuromorphic/volterra_kernel_package.hpp"
#include "../spatial/AuxGeneziRoute.hpp"
#include "../spatial/HomogeneousFusion.hpp"

namespace {

using ivanna::dsp::VolterraH2Symmetric;
using ivanna::dsp::VolterraLoadPolicy;
using ivanna::dsp::VolterraLoadStatus;
using ivanna::dsp::VolterraPackageView;
using ivanna::dsp::volterraValidatePackage;
using ivanna::spatial::AuxVolterraGate;
using ivanna::spatial::HomogeneousFusion;

constexpr uint32_t kK = 64;
constexpr uint32_t kFs = 48000;
constexpr size_t kPkgSize = 40 + 4 * 64 + 4 * (64 * 65 / 2);  // 8616

#ifndef IVANNA_GENEZY_IVK1_PATH
#define IVANNA_GENEZY_IVK1_PATH ""
#endif

std::vector<uint8_t> readPackage() {
    std::vector<std::string> candidates;
    const std::string def = IVANNA_GENEZY_IVK1_PATH;
    if (!def.empty()) candidates.push_back(def);
    candidates.push_back(
        "../../app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1");
    candidates.push_back(
        "app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1");
    for (const auto& path : candidates) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        if (b.size() == kPkgSize) return b;
    }
    return {};
}

std::vector<float> readF32(const uint8_t* p, size_t n) {
    std::vector<float> v(n);
    std::memcpy(v.data(), p, n * sizeof(float));
    return v;
}

std::vector<float> sineStereo(size_t frames, float amp, float freq) {
    std::vector<float> x(frames * 2);
    for (size_t n = 0; n < frames; ++n) {
        const float s = amp * std::sin(2.0f * 3.14159265f * freq *
                                       static_cast<float>(n) / 48000.0f);
        x[2 * n] = s;
        x[2 * n + 1] = s;
    }
    return x;
}

bool allFinite(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(),
                       [](float x) { return std::isfinite(x); });
}

float maxAbs(const std::vector<float>& v) {
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

// Referencia escalar del funcional de Volterra (un canal, historia cero):
//   y[n] = sum_k h1[k]*x[n-k]
//        + sum_{k<=l} (k==l ? 1 : 2) * h2tri[h2Index(k,l)] * x[n-k]*x[n-l]
// Es la misma matemática que VolterraH2Symmetric::processInterleaved
// (sin envelope, sin saturador: la entrada se elige para no saturar).
std::vector<float> referenceVolterra(const std::vector<float>& x,
                                     const float* h1, const float* h2tri) {
    std::vector<float> y(x.size(), 0.0f);
    std::vector<float> hist(kK, 0.0f);
    for (size_t n = 0; n < x.size(); ++n) {
        for (int k = static_cast<int>(kK) - 1; k > 0; --k) hist[k] = hist[k - 1];
        hist[0] = x[n];
        float yl = 0.0f;
        for (uint32_t k = 0; k < kK; ++k) yl += h1[k] * hist[k];
        float yq = 0.0f;
        for (uint32_t k = 0; k < kK; ++k) {
            for (uint32_t l = k; l < kK; ++l) {
                const uint32_t idx =
                    k * kK + l - (k * (k + 1)) / 2;
                const float mult = (k == l) ? 1.0f : 2.0f;
                yq += mult * h2tri[idx] * hist[k] * hist[l];
            }
        }
        y[n] = yl + yq;
    }
    return y;
}

// Ejecuta hasta que el envelope reporte silencio; devuelve el último bloque.
std::vector<float> drainToSilence(VolterraH2Symmetric& v,
                                  const std::vector<float>& in) {
    std::vector<float> out(in.size(), -7.0f);
    for (int i = 0; i < 400 && !v.isSilent(); ++i)
        v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
    v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
    return out;
}

}  // namespace

TEST(GenezyKernel, PackageExistsAndHasRepoLayout) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize) << "genera el .ivk1 con "
                                       "tools/volterra_h2/build_genezy_mhc_pz1d_ivk1.py";
    EXPECT_EQ(std::memcmp(pkg.data(), "IVK1", 4), 0);
    EXPECT_EQ(ivanna::dsp::ivk1::rdU16(&pkg[4]), 1u);   // format_version
    EXPECT_EQ(ivanna::dsp::ivk1::rdU16(&pkg[6]), kK);   // kernel_length
    EXPECT_EQ(ivanna::dsp::ivk1::rdU32(&pkg[8]), kFs);  // sample_rate
    EXPECT_EQ(std::memcmp(&pkg[16], "GENEZY01", 8), 0);
    EXPECT_EQ(std::memcmp(&pkg[24], "MOTO_G85_GENEZY", 14), 0);
}

TEST(GenezyKernel, ProductionGateAcceptsPackage) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    // (a) Puerta C++ de producción (AuxVolterraGate).
    EXPECT_EQ(AuxVolterraGate::validate(pkg.data(), pkg.size()),
              VolterraLoadStatus::Ok);
    // Misma puerta vía volterraValidatePackage con la política por defecto.
    VolterraLoadPolicy prod;
    VolterraPackageView view{};
    EXPECT_EQ(volterraValidatePackage(pkg.data(), pkg.size(), prod, &view),
              VolterraLoadStatus::Ok);
    ASSERT_NE(view.h1_bytes, nullptr);
    ASSERT_NE(view.h2_bytes, nullptr);
    const auto h1 = readF32(view.h1_bytes, kK);
    const auto h2 = readF32(view.h2_bytes, kK * (kK + 1) / 2);
    // h1 normalizada por L1: sum|h1| = 1.0 (puerta) y DC = 0.699 (-3.1 dB).
    double l1 = 0.0, dc = 0.0;
    for (float v : h1) { l1 += std::fabs(v); dc += v; }
    EXPECT_NEAR(l1, 1.0, 1e-6);
    EXPECT_NEAR(dc, 0.6992, 1e-4);
    // Coeficientes exactos del PDF (normalizados): tap 16 es el pico.
    EXPECT_NEAR(h1[16], 0.684210f / 4.391953f, 1e-6f);
    EXPECT_NEAR(h1[0], 0.001248f / 4.391953f, 1e-6f);
    // h2: 32 coeficientes no nulos (16 diagonal + 16 super-diagonal).
    size_t nz = 0;
    for (float v : h2) if (v != 0.0f) ++nz;
    EXPECT_EQ(nz, 32u);
    // Diagonal (0,0) y super-diagonal (0,1) = sub[0] = (1,0).
    EXPECT_NEAR(h2[ivanna::dsp::ivk1::h2Index(0, 0, kK)], -0.000412f, 1e-7f);
    EXPECT_NEAR(h2[ivanna::dsp::ivk1::h2Index(0, 1, kK)], 0.000125f, 1e-7f);
    EXPECT_NEAR(h2[ivanna::dsp::ivk1::h2Index(7, 7, kK)], -0.022410f, 1e-7f);
    EXPECT_NEAR(h2[ivanna::dsp::ivk1::h2Index(7, 8, kK)], 0.004120f, 1e-7f);
}

TEST(GenezyKernel, HomogeneousFusionHoldsValidatedPackage) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    HomogeneousFusion f{};
    f.prepare(48000.0f, 128);
    EXPECT_EQ(f.loadVolterraPackage(pkg.data(), pkg.size()),
              VolterraLoadStatus::Ok);
    EXPECT_TRUE(f.volterraReady());
    ASSERT_NE(f.volterraPackage(), nullptr);
    EXPECT_EQ(f.volterraPackageSize(), pkg.size());
    EXPECT_EQ(std::memcmp(f.volterraPackage(), pkg.data(), pkg.size()), 0);
}

TEST(GenezyKernel, LoadKernelPackageAppliesWithoutEnabling) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado
    VolterraLoadPolicy prod;  // producción (GENEZY01 / MOTO_G85_GENEZY)
    EXPECT_EQ(v.loadKernelPackage(pkg.data(), pkg.size(), prod),
              VolterraLoadStatus::Ok);
    EXPECT_TRUE(v.hasEffectiveKernel());
    EXPECT_FALSE(v.isEnabled()) << "cargar no activa el motor";
}

TEST(GenezyKernel, ProcessesSineFiniteBoundedAndDifferentFromIdentity) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);
    VolterraLoadPolicy prod;
    ASSERT_EQ(v.loadKernelPackage(pkg.data(), pkg.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);
    for (float f : {50.0f, 100.0f, 440.0f, 1000.0f, 5000.0f}) {
        const auto in = sineStereo(1024, 0.3f, f);
        std::vector<float> out(in.size());
        for (int i = 0; i < 16; ++i)
            v.processInterleaved(in.data(), out.data(), 1024, 2);
        ASSERT_TRUE(allFinite(out)) << f << " Hz";
        EXPECT_LE(maxAbs(out), 1.0f) << f << " Hz";
        // El kernel cambia la señal (no es identidad) de forma acotada.
        float maxDiff = 0.0f;
        for (size_t i = 0; i < in.size(); ++i)
            maxDiff = std::max(maxDiff, std::fabs(out[i] - in[i]));
        EXPECT_GT(maxDiff, 1e-3f) << f << " Hz: el kernel debe cambiar la señal";
        EXPECT_LT(maxDiff, 0.5f) << f << " Hz: cambio acotado, sin divergencia";
    }
}

TEST(GenezyKernel, OutputMatchesScalarReferenceAfterEnvelopeSettles) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    VolterraLoadPolicy prod;
    VolterraPackageView view{};
    ASSERT_EQ(volterraValidatePackage(pkg.data(), pkg.size(), prod, &view),
              VolterraLoadStatus::Ok);
    const auto h1 = readF32(view.h1_bytes, kK);
    const auto h2 = readF32(view.h2_bytes, kK * (kK + 1) / 2);

    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);
    ASSERT_EQ(v.loadKernelPackage(pkg.data(), pkg.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);

    // Seno 100 Hz, amp 0.3 (lejos del saturador de 0.90): 32 bloques de 512.
    // El envelope ataca en 8 ms (~384 muestras); el último bloque está a
    // ganancia 1.0 y debe coincidir con la referencia escalar.
    constexpr size_t kFrames = 512;
    constexpr int kBlocks = 32;
    const auto in = sineStereo(kFrames, 0.3f, 100.0f);
    std::vector<float> out(in.size());
    for (int i = 0; i < kBlocks; ++i)
        v.processInterleaved(in.data(), out.data(), kFrames, 2);

    // Referencia: mismo seno continuo (32 bloques concatenados), canal L.
    std::vector<float> mono(kFrames * kBlocks);
    for (int b = 0; b < kBlocks; ++b)
        for (size_t n = 0; n < kFrames; ++n) mono[b * kFrames + n] = in[2 * n];
    const auto ref = referenceVolterra(mono, h1.data(), h2.data());
    // Compara solo el último bloque (envelope a 1.0), canal L y R.
    for (size_t n = 0; n < kFrames; ++n) {
        const float r = ref[(kBlocks - 1) * kFrames + n];
        EXPECT_NEAR(out[2 * n], r, 1e-4f) << "L n=" << n;
        EXPECT_NEAR(out[2 * n + 1], r, 1e-4f) << "R n=" << n;
    }
}

TEST(GenezyKernel, BypassBitExactWithoutKernelAndAfterDisable) {
    // (e) Sin kernel: bypass bit-exacto aunque el motor esté "habilitado".
    {
        VolterraH2Symmetric v(kK, 2);
        EXPECT_FALSE(v.hasEffectiveKernel());
        const auto in = sineStereo(512, 0.8f, 1234.0f);
        std::vector<float> out(in.size(), -1.0f);
        for (int i = 0; i < 10; ++i) {
            v.processInterleaved(in.data(), out.data(), 512, 2);
            ASSERT_EQ(std::memcmp(in.data(), out.data(),
                                  in.size() * sizeof(float)), 0);
        }
    }
    // Con kernel cargado y procesando, al desactivar y drenar a silencio
    // la salida vuelve a ser la entrada (bypass), bit-exacta.
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);
    VolterraLoadPolicy prod;
    ASSERT_EQ(v.loadKernelPackage(pkg.data(), pkg.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);
    const auto in = sineStereo(512, 0.4f, 330.0f);
    std::vector<float> out(in.size());
    for (int i = 0; i < 10; ++i)
        v.processInterleaved(in.data(), out.data(), 512, 2);
    ASSERT_TRUE(v.isEnabled());
    v.setEnabled(false);
    const auto last = drainToSilence(v, in);
    EXPECT_TRUE(v.isSilent());
    ASSERT_TRUE(allFinite(last));
    for (size_t i = 0; i < in.size(); ++i)
        EXPECT_NEAR(last[i], in[i], 1e-6f) << "i=" << i;
}
