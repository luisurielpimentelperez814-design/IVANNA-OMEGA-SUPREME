// test_volterra_moto_g85_pz1d.cpp — Kernel Volterra H1/H2 REFINADO de la
// cadena Moto G85 5G + DAC USB-C + Sony Genezi MHC-PZ1D (paquete IVK1 del
// repo, dataset GENEZY01, K=64, fs 48 kHz, device MOTO_G85_PZ1D).
//
// El .ivk1 lo genera tools/volterra_h2/build_moto_g85_pz1d_ivk1.py desde
// la caracterización electroacústica integral compartida por Esse
// (G85 + DAC + Genezi) y vive en
// app/src/main/assets/kernels/moto_g85_pz1d.ivk1. La ruta llega por la
// definición IVANNA_PZ1D_IVK1_PATH (tests/CMakeLists.txt); si no está
// definida se prueba una ruta relativa al árbol del repo.
//
// Diferencias con el kernel Genezi anterior (genezy_mhc_pz1d.ivk1):
//   - h1 re-tabulada para la cadena completa (L1=5.00097, DC=3.76669);
//     normalizada por L1 -> DC=0.753 (-2.46 dB).
//   - h2 DIAGONAL SOLAMENTE (16 coeficientes, sin sub-diagonal).
//   - device_id "MOTO_G85_PZ1D" (autorizado explícitamente en la política
//     de producción junto a "MOTO_G85_GENEZY"; ver volterra_kernel_package.hpp).
//
// Qué verifica (en orden de la puerta -> motor):
//   (a) el .ivk1 pasa AuxVolterraGate (política de producción) — puerta C++.
//   (b) HomogeneousFusion::loadVolterraPackage lo guarda (vía de la fusión).
//   (c) VolterraH2Symmetric::loadKernelPackage lo aplica (sin habilitar).
//   (d) el motor procesa: seno finito y acotado, distinto de la identidad
//       de forma acotada, y la salida coincide con una referencia escalar
//       del funcional de Volterra con los coeficientes del paquete.
//   (e) PARIDAD optimizado-vs-escalar: el path optimizado del motor
//       (H2 disperso + H1 NEON en ARM / escalar sin módulos en host)
//       da el mismo resultado que una referencia escalar pura dentro de
//       tolerancia float, con una señal pseudo-aleatoria determinista.
//   (f) bypass bit-exacto sin kernel y tras desactivar (drain a silencio).
//   (g) throughput: mide xRT con este kernel e imprime el número
//       (la optimización dispersa debe dar un salto grande frente al
//       bucle H2 denso O(K^2); ver docs/MOTO_G85_PZ1D_KERNEL.md).
//
// El kernel es el DEFAULT de Volterra en v2.5.369 (activo por defecto;
// cargar no habilita el motor, hay que habilitarlo explícitamente con
// setEnabled(true) — en la app lo hace el restore). Fail-closed ante
// cualquier error.
#include <algorithm>
#include <chrono>
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

#ifndef IVANNA_PZ1D_IVK1_PATH
#define IVANNA_PZ1D_IVK1_PATH ""
#endif

std::vector<uint8_t> readPackage() {
    std::vector<std::string> candidates;
    const std::string def = IVANNA_PZ1D_IVK1_PATH;
    if (!def.empty()) candidates.push_back(def);
    candidates.push_back(
        "../../app/src/main/assets/kernels/moto_g85_pz1d.ivk1");
    candidates.push_back(
        "app/src/main/assets/kernels/moto_g85_pz1d.ivk1");
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

// Ruido pseudo-aleatorio determinista (LCG, semilla fija): ejercita el
// path H2 disperso con historia no trivial (sin ceros que activen el
// salto por umbral), para la prueba de paridad. L y R llevan la MISMA
// señal (como sineStereo) para que ambos canales se puedan comparar
// contra una sola referencia mono.
std::vector<float> noiseStereo(size_t frames, float amp, uint32_t seed) {
    std::vector<float> x(frames * 2);
    uint32_t s = seed;
    for (size_t n = 0; n < frames; ++n) {
        s = s * 1664525u + 1013904223u;
        const float u = static_cast<float>(s >> 8) / 16777216.0f;  // [0,1)
        const float v = amp * (2.0f * u - 1.0f);
        x[2 * n] = v;
        x[2 * n + 1] = v;
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

// Referencia escalar PURA del funcional de Volterra (un canal, historia
// cero). Es la definición matemática, sin optimizaciones:
//   y[n] = sum_k h1[k]*x[n-k]
//        + sum_{k<=l} (k==l ? 1 : 2) * h2tri[h2Index(k,l)] * x[n-k]*x[n-l]
// Sin envelope, sin saturador: la entrada se elige para no saturar.
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

TEST(Pz1dKernel, PackageExistsAndHasRepoLayout) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize) << "genera el .ivk1 con "
                                       "tools/volterra_h2/build_moto_g85_pz1d_ivk1.py";
    EXPECT_EQ(std::memcmp(pkg.data(), "IVK1", 4), 0);
    EXPECT_EQ(ivanna::dsp::ivk1::rdU16(&pkg[4]), 1u);   // format_version
    EXPECT_EQ(ivanna::dsp::ivk1::rdU16(&pkg[6]), kK);   // kernel_length
    EXPECT_EQ(ivanna::dsp::ivk1::rdU32(&pkg[8]), kFs);  // sample_rate
    EXPECT_EQ(std::memcmp(&pkg[16], "GENEZY01", 8), 0);
    EXPECT_EQ(std::memcmp(&pkg[24], "MOTO_G85_PZ1D", 13), 0);
}

TEST(Pz1dKernel, ProductionGateAcceptsPackage) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    // (a) Puerta C++ de producción (AuxVolterraGate: GENEZY + PZ1D).
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
    // h1 normalizada por L1: sum|h1| = 1.0 (puerta) y DC = 0.753 (-2.46 dB).
    double l1 = 0.0, dc = 0.0;
    for (float v : h1) { l1 += std::fabs(v); dc += v; }
    EXPECT_NEAR(l1, 1.0, 1e-6);
    EXPECT_NEAR(dc, 0.753192, 1e-4);
    // Coeficientes exactos de la caracterización (normalizados):
    // tap 15 es el pico (0.7142 / 5.00097), tap 0 el primero.
    EXPECT_NEAR(h1[15], 0.714200f / 5.00097f, 1e-6f);
    EXPECT_NEAR(h1[0], 0.001420f / 5.00097f, 1e-6f);
    EXPECT_NEAR(h1[16], 0.658410f / 5.00097f, 1e-6f);
    // h2: 16 coeficientes no nulos (SOLO diagonal; sin sub-diagonal).
    size_t nz = 0;
    for (float v : h2) if (v != 0.0f) ++nz;
    EXPECT_EQ(nz, 16u);
    EXPECT_NEAR(h2[ivanna::dsp::ivk1::h2Index(0, 0, kK)], -0.000512f, 1e-7f);
    EXPECT_NEAR(h2[ivanna::dsp::ivk1::h2Index(7, 7, kK)], -0.025800f, 1e-7f);
    EXPECT_NEAR(h2[ivanna::dsp::ivk1::h2Index(15, 15, kK)], -0.000410f, 1e-7f);
    // Sin sub-diagonal: (0,1) y (7,8) son cero (el kernel Genezi sí los tenía).
    EXPECT_EQ(h2[ivanna::dsp::ivk1::h2Index(0, 1, kK)], 0.0f);
    EXPECT_EQ(h2[ivanna::dsp::ivk1::h2Index(7, 8, kK)], 0.0f);
}

TEST(Pz1dKernel, ProductionGateStillRejectsForeignDevice) {
    // La puerta acepta GENEZY y PZ1D, pero sigue rechazando un tercero.
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    auto foreign = pkg;
    std::memcpy(&foreign[24], "OTHER_DEVICE_XYZ", 16);
    // El CRC cubre device_id: hay que re-calcularlo para que el fallo sea
    // por dispositivo y no por CRC.
    const uint32_t crc = ivanna::dsp::volterraCrc32(
        foreign.data() + ivanna::dsp::ivk1::kOffDeviceId,
        foreign.size() - ivanna::dsp::ivk1::kOffDeviceId);
    std::memcpy(&foreign[ivanna::dsp::ivk1::kOffCrc], &crc, 4);
    EXPECT_EQ(AuxVolterraGate::validate(foreign.data(), foreign.size()),
              VolterraLoadStatus::DeviceMismatch);
}

TEST(Pz1dKernel, HomogeneousFusionHoldsValidatedPackage) {
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

TEST(Pz1dKernel, LoadKernelPackageAppliesWithoutEnabling) {
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado
    VolterraLoadPolicy prod;  // producción (GENEZY01 / GENEZY + PZ1D)
    EXPECT_EQ(v.loadKernelPackage(pkg.data(), pkg.size(), prod),
              VolterraLoadStatus::Ok);
    EXPECT_TRUE(v.hasEffectiveKernel());
    EXPECT_FALSE(v.isEnabled()) << "cargar no activa el motor";
}

TEST(Pz1dKernel, ProcessesSineFiniteBoundedAndDifferentFromIdentity) {
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

TEST(Pz1dKernel, OutputMatchesScalarReferenceAfterEnvelopeSettles) {
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

TEST(Pz1dKernel, ParityOptimizedMatchesScalarReference) {
    // (e) Paridad optimizado-vs-escalar: el path optimizado del motor
    // (H2 disperso + H1 NEON en ARM / escalar sin módulos en host, línea
    // de retardo circular) debe dar el mismo resultado que la referencia
    // escalar pura, con una señal pseudo-aleatoria que ejercita toda la
    // historia (sin ceros que activen el salto por umbral).
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

    // Ruido amp 0.5, 32 bloques de 512 (mismo esquema que el seno).
    constexpr size_t kFrames = 512;
    constexpr int kBlocks = 32;
    const auto in = noiseStereo(kFrames, 0.5f, 1234567u);
    std::vector<float> out(in.size());
    for (int i = 0; i < kBlocks; ++i)
        v.processInterleaved(in.data(), out.data(), kFrames, 2);

    std::vector<float> mono(kFrames * kBlocks);
    for (int b = 0; b < kBlocks; ++b)
        for (size_t n = 0; n < kFrames; ++n) mono[b * kFrames + n] = in[2 * n];
    const auto ref = referenceVolterra(mono, h1.data(), h2.data());
    float maxErr = 0.0f;
    for (size_t n = 0; n < kFrames; ++n) {
        const float r = ref[(kBlocks - 1) * kFrames + n];
        maxErr = std::max(maxErr, std::fabs(out[2 * n] - r));
        maxErr = std::max(maxErr, std::fabs(out[2 * n + 1] - r));
        EXPECT_NEAR(out[2 * n], r, 1e-4f) << "L n=" << n;
        EXPECT_NEAR(out[2 * n + 1], r, 1e-4f) << "R n=" << n;
    }
    // El error máximo debe ser del orden del redondeo float, no de un
    // término H2 omitido (un término diagonal omitido daría ~1e-3).
    EXPECT_LT(maxErr, 1e-4f) << "paridad optimizado-vs-escalar";
}

TEST(Pz1dKernel, BypassBitExactWithoutKernelAndAfterDisable) {
    // (f) Sin kernel: bypass bit-exacto aunque el motor esté "habilitado".
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

TEST(Pz1dKernel, ThroughputRt) {
    // (g) Throughput con el kernel PZ1D (estéreo, 48 kHz). Imprime xRT.
    // La cota es holgada (>>1) para no flakear en máquinas cargadas; el
    // número real se documenta en docs/MOTO_G85_PZ1D_KERNEL.md.
    const auto pkg = readPackage();
    ASSERT_EQ(pkg.size(), kPkgSize);
    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);
    VolterraLoadPolicy prod;
    ASSERT_EQ(v.loadKernelPackage(pkg.data(), pkg.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);

    constexpr size_t kFrames = 1024;
    constexpr int kWarmup = 16;
    constexpr int kTimed = 200;  // 200*1024 = 204800 frames = 4.27 s
    const auto in = sineStereo(kFrames, 0.3f, 440.0f);
    std::vector<float> out(in.size());
    for (int i = 0; i < kWarmup; ++i)
        v.processInterleaved(in.data(), out.data(), kFrames, 2);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kTimed; ++i)
        v.processInterleaved(in.data(), out.data(), kFrames, 2);
    const auto t1 = std::chrono::steady_clock::now();
    const double wall =
        std::chrono::duration<double>(t1 - t0).count();
    const double audio = static_cast<double>(kTimed * kFrames) / 48000.0;
    const double rt = audio / wall;
    std::printf("[Pz1dKernel.ThroughputRt] %.1fx RT (audio %.2fs en %.3fs pared)\n",
                rt, audio, wall);
    ASSERT_TRUE(allFinite(out));
    EXPECT_GT(rt, 10.0) << "el motor optimizado debe ir muy por encima de tiempo real";
}
