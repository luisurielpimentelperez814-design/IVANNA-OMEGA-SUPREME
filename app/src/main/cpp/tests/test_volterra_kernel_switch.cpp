// test_volterra_kernel_switch.cpp — Cambio de kernel Volterra en runtime
// (selector PZ1D / Genezi, v2.5.369).
//
// Qué verifica (vía nueva VolterraH2Symmetric::replaceKernelPackage):
//   (a) reemplazo PZ1D -> Genezi -> PZ1D con audio sonando (el kernel que
//       suena cambia de verdad: la salida en régimen coincide con la del
//       kernel nuevo, no con la del anterior).
//   (b) un paquete inválido (CRC roto, truncado) se rechaza y el kernel
//       anterior queda intacto y sonando.
//   (c) un paquete de dispositivo ajeno lo rechaza la puerta (DeviceMismatch)
//       y el kernel anterior queda intacto.
//   (d) cambio sin audio fluyendo (música en pausa): la vía detecta la
//       ganancia congelada y aplica igual (el hueco de la pausa enmascara
//       la transición); no devuelve EngineBusy.
//   (e) cambio con audio fluyendo de verdad (hilo alimentador procesando
//       bloques mientras el hilo de control reemplaza): sin carreras,
//       sin NaN, sin clic (salida finita y acotada tras el cambio).
//   (f) la identidad del kernel activo (activeKernelDeviceId) sigue a cada
//       carga/reemplazo y se vacía con kernels crudos sin procedencia.
//   (g) replace en un motor virgen se comporta como primera carga.
//
// Los .ivk1 llegan por IVANNA_PZ1D_IVK1_PATH / IVANNA_GENEZY_IVK1_PATH
// (tests/CMakeLists.txt); si no están definidos se prueba una ruta
// relativa al árbol del repo. Fail-closed ante cualquier error.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../neuromorphic/volterra_kernel_package.hpp"

namespace {

using ivanna::dsp::VolterraH2Symmetric;
using ivanna::dsp::VolterraLoadPolicy;
using ivanna::dsp::VolterraLoadStatus;
using ivanna::dsp::volterraCrc32;

constexpr uint32_t kK = 64;
constexpr uint32_t kFs = 48000;
constexpr size_t kPkgSize = 40 + 4 * 64 + 4 * (64 * 65 / 2);  // 8616

#ifndef IVANNA_PZ1D_IVK1_PATH
#define IVANNA_PZ1D_IVK1_PATH ""
#endif
#ifndef IVANNA_GENEZY_IVK1_PATH
#define IVANNA_GENEZY_IVK1_PATH ""
#endif

std::vector<uint8_t> readPackage(const char* defPath,
                                 const char* relA, const char* relB) {
    std::vector<std::string> candidates;
    const std::string def = defPath;
    if (!def.empty()) candidates.push_back(def);
    candidates.push_back(relA);
    candidates.push_back(relB);
    for (const auto& path : candidates) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
        if (b.size() == kPkgSize) return b;
    }
    return {};
}

std::vector<uint8_t> readPz1d() {
    return readPackage(IVANNA_PZ1D_IVK1_PATH,
                       "../../app/src/main/assets/kernels/moto_g85_pz1d.ivk1",
                       "app/src/main/assets/kernels/moto_g85_pz1d.ivk1");
}

std::vector<uint8_t> readGenezi() {
    return readPackage(IVANNA_GENEZY_IVK1_PATH,
                       "../../app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1",
                       "app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1");
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

// Ejecuta bloques hasta que el envelope reporte silencio.
void drainToSilence(VolterraH2Symmetric& v, const std::vector<float>& in) {
    std::vector<float> out(in.size(), 0.0f);
    for (int i = 0; i < 400 && !v.isSilent(); ++i)
        v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
}

// Procesa `blocks` bloques de 128 frames y devuelve la salida del último.
std::vector<float> runBlocks(VolterraH2Symmetric& v,
                             const std::vector<float>& in, int blocks) {
    std::vector<float> out(in.size(), 0.0f);
    for (int i = 0; i < blocks; ++i)
        v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
    return out;
}

// Salida en régimen (envelope a ganancia 1) de un motor con `pkg` aplicado.
std::vector<float> steadyOutput(const std::vector<uint8_t>& pkg,
                                const std::vector<float>& in) {
    VolterraH2Symmetric ref(kK, 2);
    VolterraLoadPolicy prod;
    ref.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    EXPECT_EQ(ref.loadKernelPackage(pkg.data(), pkg.size(), prod),
              VolterraLoadStatus::Ok);
    ref.setEnabled(true);
    return runBlocks(ref, in, 32);
}

}  // namespace

TEST(KernelSwitch, ReplaceOnFreshEngineBehavesLikeFirstLoad) {
    const auto pz1d = readPz1d();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    VolterraH2Symmetric v(kK, 2);
    EXPECT_STREQ(v.activeKernelDeviceId(), "");
    EXPECT_FALSE(v.hasEffectiveKernel());
    VolterraLoadPolicy prod;
    EXPECT_EQ(v.replaceKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    EXPECT_TRUE(v.hasEffectiveKernel());
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_PZ1D");
}

TEST(KernelSwitch, ReplacePz1dWithGeneziAndBack) {
    const auto pz1d = readPz1d();
    const auto genezi = readGenezi();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    ASSERT_EQ(genezi.size(), kPkgSize);
    const auto in = sineStereo(128, 0.5f, 1000.0f);
    VolterraLoadPolicy prod;

    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    ASSERT_EQ(v.loadKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    ASSERT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_PZ1D");
    v.setEnabled(true);
    const auto outPz1d = runBlocks(v, in, 32);
    ASSERT_TRUE(allFinite(outPz1d));
    ASSERT_LT(maxAbs(outPz1d), 1.0f);

    // Reemplazo a Genezi (vía nueva; el motor se deshabilita y fundea solo).
    ASSERT_EQ(v.replaceKernelPackage(genezi.data(), genezi.size(), prod),
              VolterraLoadStatus::Ok);
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_GENEZY");
    EXPECT_TRUE(v.hasEffectiveKernel());
    v.setEnabled(true);
    const auto outGenezi = runBlocks(v, in, 32);
    ASSERT_TRUE(allFinite(outGenezi));
    ASSERT_LT(maxAbs(outGenezi), 1.0f);

    // La salida en régimen es la del Genezi, no la del PZ1D: debe coincidir
    // con un motor Genezi de referencia y diferir del PZ1D.
    const auto refGenezi = steadyOutput(genezi, in);
    const auto refPz1d = steadyOutput(pz1d, in);
    float diffGenezi = 0.0f, diffPz1d = 0.0f;
    for (size_t i = 0; i < outGenezi.size(); ++i) {
        diffGenezi = std::max(diffGenezi, std::fabs(outGenezi[i] - refGenezi[i]));
        diffPz1d = std::max(diffPz1d, std::fabs(outGenezi[i] - refPz1d[i]));
    }
    EXPECT_LT(diffGenezi, 1e-4f) << "tras el reemplazo suena el Genezi";
    EXPECT_GT(diffPz1d, 1e-3f) << "tras el reemplazo ya no suena el PZ1D";

    // Vuelta al PZ1D.
    ASSERT_EQ(v.replaceKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_PZ1D");
    v.setEnabled(true);
    const auto outBack = runBlocks(v, in, 32);
    ASSERT_TRUE(allFinite(outBack));
    float diffBack = 0.0f;
    for (size_t i = 0; i < outBack.size(); ++i)
        diffBack = std::max(diffBack, std::fabs(outBack[i] - refPz1d[i]));
    EXPECT_LT(diffBack, 1e-4f) << "de vuelta al PZ1D suena el PZ1D";
}

TEST(KernelSwitch, InvalidPackageKeepsPrevious) {
    const auto pz1d = readPz1d();
    const auto genezi = readGenezi();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    ASSERT_EQ(genezi.size(), kPkgSize);
    const auto in = sineStereo(128, 0.5f, 1000.0f);
    VolterraLoadPolicy prod;

    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    ASSERT_EQ(v.loadKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);
    runBlocks(v, in, 32);
    ASSERT_FALSE(v.isSilent());

    // (a) CRC roto: un byte de h1 cambiado (el CRC cubre [24, fin)).
    auto badCrc = genezi;
    badCrc[40] ^= 0xFF;
    EXPECT_EQ(v.replaceKernelPackage(badCrc.data(), badCrc.size(), prod),
              VolterraLoadStatus::CrcMismatch);
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_PZ1D");
    EXPECT_TRUE(v.hasEffectiveKernel());

    // (b) Truncado.
    EXPECT_EQ(v.replaceKernelPackage(genezi.data(), 100, prod),
              VolterraLoadStatus::Truncated);
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_PZ1D");
    EXPECT_TRUE(v.hasEffectiveKernel());

    // El PZ1D sigue sonando: re-habilitar y medir salida activa y acotada.
    v.setEnabled(true);
    const auto out = runBlocks(v, in, 32);
    ASSERT_TRUE(allFinite(out));
    ASSERT_LT(maxAbs(out), 1.0f);
    const auto refPz1d = steadyOutput(pz1d, in);
    float diff = 0.0f;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max(diff, std::fabs(out[i] - refPz1d[i]));
    EXPECT_LT(diff, 1e-4f) << "tras dos rechazos sigue sonando el PZ1D";
}

TEST(KernelSwitch, ForeignDeviceRejectedKeepsPrevious) {
    const auto pz1d = readPz1d();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    const auto in = sineStereo(128, 0.5f, 1000.0f);
    VolterraLoadPolicy prod;

    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    ASSERT_EQ(v.loadKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);
    runBlocks(v, in, 32);

    // Paquete con device ajeno y CRC re-calculado (el ÚNICO fallo debe
    // ser DeviceMismatch: si el CRC estuviera roto daría CrcMismatch).
    auto foreignPkt = pz1d;
    // Escritura por índice (no memset/memcpy sobre data()+offset: GCC 14
    // con _FORTIFY_SOURCE emite un falso -Warray-bounds sobre el vector).
    const char* foreignId = "FOREIGN_DEVICE!";
    for (int i = 0; i < 16; ++i)
        foreignPkt[24 + i] = (i < 15) ? static_cast<uint8_t>(foreignId[i]) : 0;
    const uint32_t crc = volterraCrc32(foreignPkt.data() + 24, foreignPkt.size() - 24);
    std::memcpy(foreignPkt.data() + 12, &crc, 4);
    EXPECT_EQ(v.replaceKernelPackage(foreignPkt.data(), foreignPkt.size(), prod),
              VolterraLoadStatus::DeviceMismatch);
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_PZ1D");
    EXPECT_TRUE(v.hasEffectiveKernel());
    // El motor ni siquiera se deshabilitó: sigue sonando sin interrupción.
    EXPECT_TRUE(v.isEnabled());
}

TEST(KernelSwitch, IdlePathSucceedsWithNoAudioFlowing) {
    const auto pz1d = readPz1d();
    const auto genezi = readGenezi();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    ASSERT_EQ(genezi.size(), kPkgSize);
    const auto in = sineStereo(128, 0.5f, 1000.0f);
    VolterraLoadPolicy prod;

    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    ASSERT_EQ(v.loadKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);
    runBlocks(v, in, 32);
    ASSERT_FALSE(v.isSilent());  // envelope a ganancia 1

    // Música en pausa: NO se procesa ningún bloque más. El reemplazo
    // debe detectar la ganancia congelada y aplicar igual (sin
    // EngineBusy): el hueco de la pausa enmascara la transición.
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(v.replaceKernelPackage(genezi.data(), genezi.size(), prod),
              VolterraLoadStatus::Ok);
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_GENEZY");
    EXPECT_TRUE(v.hasEffectiveKernel());
    EXPECT_LT(dt, 300) << "el camino sin-audio no debe agotar el tope";
    EXPECT_GE(dt, 40) << "debió esperar la gracia de inactividad (~50 ms)";

    // Al reanudar el audio suena el Genezi.
    v.setEnabled(true);
    const auto out = runBlocks(v, in, 32);
    ASSERT_TRUE(allFinite(out));
    const auto refGenezi = steadyOutput(genezi, in);
    float diff = 0.0f;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max(diff, std::fabs(out[i] - refGenezi[i]));
    EXPECT_LT(diff, 1e-4f);
}

TEST(KernelSwitch, FlowingAudioSwitch) {
    const auto pz1d = readPz1d();
    const auto genezi = readGenezi();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    ASSERT_EQ(genezi.size(), kPkgSize);
    VolterraLoadPolicy prod;

    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    ASSERT_EQ(v.loadKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);

    // Hilo "audio": bloques de 128 frames de seno en bucle, como el
    // callback real. Registra si algo sale no-finito o fuera de rango.
    std::atomic<bool> stop{false};
    std::atomic<bool> sawNonFinite{false};
    std::atomic<float> feederMaxAbs{0.0f};
    const auto in = sineStereo(128, 0.5f, 1000.0f);
    std::thread feeder([&] {
        std::vector<float> out(in.size(), 0.0f);
        while (!stop.load(std::memory_order_acquire)) {
            v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
            if (!allFinite(out)) sawNonFinite.store(true, std::memory_order_release);
            float m = maxAbs(out);
            float prev = feederMaxAbs.load(std::memory_order_relaxed);
            while (m > prev &&
                   !feederMaxAbs.compare_exchange_weak(prev, m, std::memory_order_relaxed)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // Dejar que el envelope llegue a ganancia 1 con audio fluyendo.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    EXPECT_FALSE(v.isSilent());

    // El reemplazo (hilo de control) con audio fluyendo de verdad.
    const auto t0 = std::chrono::steady_clock::now();
    const VolterraLoadStatus st =
        v.replaceKernelPackage(genezi.data(), genezi.size(), prod);
    const auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    EXPECT_EQ(st, VolterraLoadStatus::Ok) << "el cambio con audio debe funcionar";
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_GENEZY");
    EXPECT_LT(dt, 300);

    stop.store(true, std::memory_order_release);
    feeder.join();
    EXPECT_FALSE(sawNonFinite.load()) << "el alimentador nunca vio NaN/Inf";
    EXPECT_LT(feederMaxAbs.load(), 1.0f) << "salida acotada durante el cambio";

    // Tras el cambio suena el Genezi.
    v.setEnabled(true);
    const auto out = runBlocks(v, in, 32);
    ASSERT_TRUE(allFinite(out));
    const auto refGenezi = steadyOutput(genezi, in);
    float diff = 0.0f;
    for (size_t i = 0; i < out.size(); ++i)
        diff = std::max(diff, std::fabs(out[i] - refGenezi[i]));
    EXPECT_LT(diff, 1e-4f);
}

TEST(KernelSwitch, DeviceIdTracking) {
    const auto pz1d = readPz1d();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    const auto in = sineStereo(128, 0.5f, 1000.0f);
    VolterraLoadPolicy prod;

    VolterraH2Symmetric v(kK, 2);
    EXPECT_STREQ(v.activeKernelDeviceId(), "");
    v.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    ASSERT_EQ(v.loadKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    EXPECT_STREQ(v.activeKernelDeviceId(), "MOTO_G85_PZ1D");

    // Kernels crudos (sin paquete) no tienen procedencia: la identidad se vacía.
    v.setEnabled(true);
    runBlocks(v, in, 32);
    v.setEnabled(false);
    drainToSilence(v, in);
    ASSERT_TRUE(v.isSilent());
    std::vector<float> h1(kK, 0.0f), h2((kK * (kK + 1)) / 2, 0.0f);
    h1[0] = 1.0f;
    EXPECT_EQ(v.updateKernelsChecked(h1.data(), h2.data(), kK),
              VolterraLoadStatus::Ok);
    EXPECT_STREQ(v.activeKernelDeviceId(), "");
}

TEST(KernelSwitch, NoClickBoundedTransition) {
    const auto pz1d = readPz1d();
    const auto genezi = readGenezi();
    ASSERT_EQ(pz1d.size(), kPkgSize);
    ASSERT_EQ(genezi.size(), kPkgSize);
    VolterraLoadPolicy prod;

    VolterraH2Symmetric v(kK, 2);
    v.setEnabled(false);  // contrato: cargar con el motor apagado (m_enabled nace en true)
    ASSERT_EQ(v.loadKernelPackage(pz1d.data(), pz1d.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);
    // Seno continuo (misma fase en todo el test): 4 bloques de pre-roll.
    std::vector<float> in;
    {
        const auto s = sineStereo(128, 0.5f, 1000.0f);
        for (int b = 0; b < 4; ++b) in.insert(in.end(), s.begin(), s.end());
    }
    std::vector<float> out(in.size(), 0.0f);
    v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
    ASSERT_TRUE(allFinite(out));

    // Cambio (camino sin-audio: un solo hilo) y reanudación con la misma
    // señal. El envelope fundea de vuelta en 8 ms: sin salto de escala.
    ASSERT_EQ(v.replaceKernelPackage(genezi.data(), genezi.size(), prod),
              VolterraLoadStatus::Ok);
    v.setEnabled(true);
    std::vector<float> out2(in.size(), 0.0f);
    v.processInterleaved(in.data(), out2.data(), in.size() / 2, 2);
    ASSERT_TRUE(allFinite(out2));
    ASSERT_LT(maxAbs(out2), 1.0f);
    float maxJump = 0.0f;
    float prev = out.back();
    for (float y : out2) {
        maxJump = std::max(maxJump, std::fabs(y - prev));
        prev = y;
    }
    EXPECT_LT(maxJump, 0.5f) << "sin clic de escala completa al reanudar";
}
