// Pruebas del validador/cargador de kernels Volterra (contenedor IVK1) y de la
// carga segura en VolterraH2Symmetric. TODOS los coeficientes son SINTÉTICOS
// (dataset_id "SYNTH000", device de prueba): validan el cargador, NO son
// calibración física del Genezy. Incluye un paquete generado por
// tools/volterra_h2/kernel_loader.py (interoperabilidad Python <-> C++).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../neuromorphic/volterra_kernel_package.hpp"

using ivanna::dsp::VolterraH2Symmetric;
using ivanna::dsp::VolterraLoadPolicy;
using S = ivanna::dsp::VolterraLoadStatus;
namespace ivk1 = ivanna::dsp::ivk1;

namespace {

constexpr uint32_t K = 16;
constexpr const char* kTestDevice = "TEST_DEVICE_0001";  // 16 caracteres: sin NUL final

// Generado por tools/volterra_h2/kernel_loader.py::build_payload(K=16, fs=48000,
// device="TEST_DEVICE_0001", dataset_id=b"SYNTH000", h1=[0.9,0.05,-0.03,0..],
// h2[0]=0.02, h2[1]=-0.005, h2[K]=0.01).
const uint8_t kPythonFixture[] = {73,86,75,49,1,0,16,0,128,187,0,0,35,54,179,113,83,89,78,84,72,48,48,48,84,69,83,84,95,68,69,86,73,67,69,95,48,48,48,49,102,102,102,63,205,204,76,61,143,194,245,188,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,10,215,163,60,10,215,163,187,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,10,215,35,60,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

struct PkgSpec {
    uint32_t K = ::K;
    uint32_t fs = 48000;
    uint16_t fmt = ivk1::kFormatVersion;
    std::string dataset = ivk1::kDatasetSynthetic;
    std::string device = kTestDevice;
    std::vector<float> h1;
    std::vector<float> h2;   // triangular superior
};

void put32(std::vector<uint8_t>& b, size_t off, uint32_t v) { std::memcpy(&b[off], &v, 4); }
void fixCrc(std::vector<uint8_t>& b) {
    put32(b, ivk1::kOffCrc, ivanna::dsp::volterraCrc32(b.data() + ivk1::kOffDeviceId,
                                                        b.size() - ivk1::kOffDeviceId));
}
std::vector<float> defaultH1(uint32_t k) {
    std::vector<float> h(k, 0.0f);
    h[0] = 0.8f; if (k > 1) h[1] = 0.1f; if (k > 2) h[2] = -0.05f;
    return h;
}
std::vector<float> defaultH2(uint32_t k) {
    std::vector<float> h(static_cast<size_t>(k) * (k + 1) / 2, 0.0f);
    h[ivk1::h2Index(0, 0, k)] = 0.02f;
    if (k > 1) h[ivk1::h2Index(0, 1, k)] = -0.005f;
    if (k > 2) h[ivk1::h2Index(1, 1, k)] = 0.01f;
    return h;
}
std::vector<uint8_t> makePkg(PkgSpec s = {}) {
    if (s.h1.empty()) s.h1 = defaultH1(s.K);
    if (s.h2.empty()) s.h2 = defaultH2(s.K);
    std::vector<uint8_t> b(ivk1::kHeaderSize + 4 * (s.h1.size() + s.h2.size()), 0);
    std::memcpy(&b[ivk1::kOffMagic], ivk1::kMagic, 4);
    std::memcpy(&b[ivk1::kOffVersion], &s.fmt, 2);
    const uint16_t k16 = static_cast<uint16_t>(s.K);
    std::memcpy(&b[ivk1::kOffKernelLen], &k16, 2);
    put32(b, ivk1::kOffSampleRate, s.fs);
    std::memcpy(&b[ivk1::kOffDatasetId], s.dataset.c_str(), std::min<size_t>(s.dataset.size(), 8));
    std::memcpy(&b[ivk1::kOffDeviceId], s.device.c_str(), std::min<size_t>(s.device.size(), 16));
    std::memcpy(&b[ivk1::kHeaderSize], s.h1.data(), 4 * s.h1.size());
    std::memcpy(&b[ivk1::kHeaderSize + 4 * s.h1.size()], s.h2.data(), 4 * s.h2.size());
    fixCrc(b);
    return b;
}
VolterraLoadPolicy testPolicy(uint32_t k = K) {
    VolterraLoadPolicy p;
    p.expected_kernel_length = k;
    std::strncpy(p.expected_device_id, kTestDevice, sizeof(p.expected_device_id) - 1);
    p.allow_synthetic = true;  // SÓLO en pruebas
    return p;
}
S load(VolterraH2Symmetric& v, const std::vector<uint8_t>& b, const VolterraLoadPolicy& p) {
    return v.loadKernelPackage(b.data(), b.size(), p);
}

std::vector<float> sine(size_t frames, float amp, float f) {
    std::vector<float> x(frames * 2);
    for (size_t n = 0; n < frames; ++n) {
        const float s = amp * std::sin(2.0f * 3.14159265f * f * static_cast<float>(n) / 48000.0f);
        x[2 * n] = s; x[2 * n + 1] = s;
    }
    return x;
}

bool allFinite(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}

// Ejecuta hasta que el envelope reporte silencio; devuelve el último bloque.
std::vector<float> drainToSilence(VolterraH2Symmetric& v, const std::vector<float>& in) {
    std::vector<float> out(in.size(), -7.0f);
    for (int i = 0; i < 400 && !v.isSilent(); ++i) v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
    v.processInterleaved(in.data(), out.data(), in.size() / 2, 2);
    return out;
}

} // namespace

TEST(VolterraPackage, Crc32KnownVector) {
    const char* s = "123456789";
    EXPECT_EQ(ivanna::dsp::volterraCrc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
}

TEST(VolterraPackage, LayoutMatchesKernelLoaderPy) {
    const auto b = makePkg();
    EXPECT_EQ(b.size(), 40u + 4u * (K + K * (K + 1) / 2));
    EXPECT_EQ(std::memcmp(b.data(), "IVK1", 4), 0);
    EXPECT_EQ(ivk1::rdU16(&b[4]), 1u);
    EXPECT_EQ(ivk1::rdU16(&b[6]), K);
    EXPECT_EQ(ivk1::rdU32(&b[8]), 48000u);
    EXPECT_EQ(sizeof(kPythonFixture), b.size());
}

TEST(VolterraPackage, PythonGeneratedPackageIsAcceptedAndMatchesCppBuilder) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    EXPECT_EQ(v.loadKernelPackage(kPythonFixture, sizeof(kPythonFixture), testPolicy()), S::Ok);
    EXPECT_TRUE(v.hasEffectiveKernel());
    // CRC y layout idénticos byte a byte para los mismos coeficientes.
    PkgSpec s; s.h1.assign(K, 0.0f); s.h1[0] = 0.9f; s.h1[1] = 0.05f; s.h1[2] = -0.03f;
    s.h2.assign(K * (K + 1) / 2, 0.0f); s.h2[0] = 0.02f; s.h2[1] = -0.005f; s.h2[K] = 0.01f;
    const auto b = makePkg(s);
    ASSERT_EQ(b.size(), sizeof(kPythonFixture));
    EXPECT_EQ(std::memcmp(b.data(), kPythonFixture, b.size()), 0);
}

TEST(VolterraPackage, ProductionPolicyRejectsSyntheticPublicAndForeignDevice) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    VolterraLoadPolicy prod;  // defaults de producción
    EXPECT_EQ(load(v, makePkg(), prod), S::SyntheticRejected);
    // El CRC no cubre dataset_id: cambiarlo no invalida el CRC, la política sí lo rechaza.
    auto pub = makePkg();
    std::memcpy(&pub[ivk1::kOffDatasetId], "DATASET0", 8);
    EXPECT_EQ(load(v, pub, testPolicy()), S::ForeignDatasetRejected);
    EXPECT_EQ(load(v, pub, prod), S::ForeignDatasetRejected);
    PkgSpec g; g.dataset = "GENEZY01";
    EXPECT_EQ(load(v, makePkg(g), prod), S::DeviceMismatch);   // dispositivo ajeno
    PkgSpec u; u.dataset = "OTHER";
    EXPECT_EQ(load(v, makePkg(u), testPolicy()), S::UnknownDataset);
    EXPECT_FALSE(v.hasEffectiveKernel());
}

TEST(VolterraPackage, MeasuredDatasetWithAuthorizedDeviceIsAcceptedByProductionPolicy) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    PkgSpec g; g.dataset = "GENEZY01"; g.device = "MOTO_G85_GENEZY";
    VolterraLoadPolicy prod;   // sin allow_synthetic
    EXPECT_EQ(load(v, makePkg(g), prod), S::Ok);
    EXPECT_TRUE(v.hasEffectiveKernel());
    EXPECT_FALSE(v.isEnabled()) << "cargar no activa el motor";
}

TEST(VolterraPackage, CorruptedCrcAndPayloadBitFlipRejected) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    const auto b = makePkg();
    auto c = b; c[ivk1::kOffCrc] ^= 0x01;
    EXPECT_EQ(load(v, c, testPolicy()), S::CrcMismatch);
    auto f = b; f[ivk1::kHeaderSize + 5] ^= 0x10;
    EXPECT_EQ(load(v, f, testPolicy()), S::CrcMismatch);
    auto g = b; g[ivk1::kHeaderSize + 4 * K + 9] ^= 0x01;
    EXPECT_EQ(load(v, g, testPolicy()), S::CrcMismatch);
    auto d = b; d[ivk1::kOffDeviceId + 3] ^= 0x01;
    EXPECT_EQ(load(v, d, testPolicy()), S::CrcMismatch);
    EXPECT_FALSE(v.hasEffectiveKernel());
}

TEST(VolterraPackage, TruncatedOversizeNullAndTinyRejected) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    auto b = makePkg();
    EXPECT_EQ(v.loadKernelPackage(b.data(), b.size() - 1, testPolicy()), S::Truncated);
    EXPECT_EQ(v.loadKernelPackage(b.data(), 20, testPolicy()), S::Truncated);
    auto o = b; o.push_back(0);
    EXPECT_EQ(load(v, o, testPolicy()), S::SizeMismatch);
    EXPECT_EQ(v.loadKernelPackage(nullptr, 0, testPolicy()), S::NullArgument);
}

TEST(VolterraPackage, BadMagicAndVersionRejected) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    auto m = makePkg(); m[0] = 'X';
    EXPECT_EQ(load(v, m, testPolicy()), S::BadMagic);
    PkgSpec f; f.fmt = 2;
    EXPECT_EQ(load(v, makePkg(f), testPolicy()), S::BadVersion);
}

TEST(VolterraPackage, IncompatibleKernelLengthSampleRateDeviceRejected) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    PkgSpec k; k.K = 32;
    EXPECT_EQ(load(v, makePkg(k), testPolicy()), S::KernelLengthMismatch);
    PkgSpec fs; fs.fs = 44100;
    EXPECT_EQ(load(v, makePkg(fs), testPolicy()), S::SampleRateMismatch);
    PkgSpec dv; dv.device = "OTHER_DEVICE";
    EXPECT_EQ(load(v, makePkg(dv), testPolicy()), S::DeviceMismatch);
    PkgSpec dp; dp.device = "TEST_DEVICE_0001X";   // más largo que el campo: se trunca a 16 y coincide solo si es prefijo exacto
    EXPECT_EQ(load(v, makePkg(dp), testPolicy()), S::Ok);  // los 16 primeros bytes son iguales
    v.setEnabled(false);
    // El K del motor manda sobre la política.
    EXPECT_EQ(load(v, makePkg(), testPolicy(64)), S::Ok);
}

TEST(VolterraPackage, NonFiniteCoefficientsRejected) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    PkgSpec a; a.h1 = defaultH1(K); a.h1[3] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(load(v, makePkg(a), testPolicy()), S::NonFinite);
    PkgSpec b; b.h2 = defaultH2(K); b.h2[7] = std::numeric_limits<float>::infinity();
    EXPECT_EQ(load(v, makePkg(b), testPolicy()), S::NonFinite);
}

TEST(VolterraPackage, UnstableH1AmplitudeAndH2BudgetRejected) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    PkgSpec u; u.h1.assign(K, 0.0f); u.h1[0] = 0.9f; u.h1[1] = 0.2f;   // l1 = 1.1 > 1
    EXPECT_EQ(load(v, makePkg(u), testPolicy()), S::UnstableH1);
    PkgSpec big; big.h1.assign(K, 0.0f); big.h1[0] = 9.0f;             // |coef| > 8
    EXPECT_EQ(load(v, makePkg(big), testPolicy()), S::AmplitudeLimit);
    PkgSpec h; h.h2.assign(K * (K + 1) / 2, 0.0f); h.h2[0] = 0.5f;     // A^2*0.5 = 0.5 > 0.25
    EXPECT_EQ(load(v, makePkg(h), testPolicy()), S::H2BudgetExceeded);
    PkgSpec e; e.h2.assign(K * (K + 1) / 2, 0.0f); e.h2[0] = 7.0f;     // energía 49 <= 64 pero presupuesto
    EXPECT_EQ(load(v, makePkg(e), testPolicy()), S::H2BudgetExceeded);
    VolterraLoadPolicy small = testPolicy(); small.max_kernel_length = 8;
    EXPECT_EQ(load(v, makePkg(), small), S::H2TooLarge);
    EXPECT_FALSE(v.hasEffectiveKernel());
}

TEST(VolterraPackage, BusyEngineReportsEngineBusyAndBypasses) {
    VolterraH2Symmetric v(K, 2);  // m_enabled == true por defecto
    EXPECT_EQ(load(v, makePkg(), testPolicy()), S::EngineBusy);
    EXPECT_FALSE(v.isEnabled()) << "ante error el motor queda en bypass";
    EXPECT_FALSE(v.hasEffectiveKernel());
    // Tras el bypass y estando silencioso, el mismo paquete entra.
    EXPECT_EQ(load(v, makePkg(), testPolicy()), S::Ok);
}

TEST(VolterraPackage, UnalignedBufferLoadsIdentically) {
    VolterraH2Symmetric a(K, 2), b(K, 2);
    a.setEnabled(false); b.setEnabled(false);
    const auto pkg = makePkg();
    std::vector<uint8_t> shifted(pkg.size() + 1);
    std::memcpy(shifted.data() + 1, pkg.data(), pkg.size());
    EXPECT_EQ(a.loadKernelPackage(pkg.data(), pkg.size(), testPolicy()), S::Ok);
    EXPECT_EQ(b.loadKernelPackage(shifted.data() + 1, pkg.size(), testPolicy()), S::Ok);
    a.setEnabled(true); b.setEnabled(true);
    const auto in = sine(512, 0.5f, 440.0f);
    std::vector<float> oa(in.size()), ob(in.size());
    for (int i = 0; i < 20; ++i) {
        a.processInterleaved(in.data(), oa.data(), 512, 2);
        b.processInterleaved(in.data(), ob.data(), 512, 2);
    }
    EXPECT_EQ(std::memcmp(oa.data(), ob.data(), oa.size() * sizeof(float)), 0);
}

TEST(VolterraPackage, SafeBypassAfterFailedLoadWhileRunning) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    ASSERT_EQ(load(v, makePkg(), testPolicy()), S::Ok);
    v.setEnabled(true);
    const auto in = sine(512, 0.4f, 330.0f);
    std::vector<float> out(in.size());
    for (int i = 0; i < 10; ++i) v.processInterleaved(in.data(), out.data(), 512, 2);
    ASSERT_TRUE(v.isEnabled());

    auto bad = makePkg(); bad[ivk1::kHeaderSize + 1] ^= 0x40;  // payload alterado -> CRC roto
    EXPECT_EQ(load(v, bad, testPolicy()), S::CrcMismatch);
    EXPECT_FALSE(v.isEnabled());
    EXPECT_TRUE(v.hasEffectiveKernel()) << "los kernels previos no se tocan";

    const auto last = drainToSilence(v, in);
    EXPECT_TRUE(v.isSilent());
    ASSERT_TRUE(allFinite(last));
    for (size_t i = 0; i < in.size(); ++i) EXPECT_NEAR(last[i], in[i], 1e-6f) << "i=" << i;
}

TEST(VolterraPackage, LoadedKernelProcessesFiniteAndBounded) {
    VolterraH2Symmetric v(K, 2);
    v.setEnabled(false);
    ASSERT_EQ(load(v, makePkg(), testPolicy()), S::Ok);
    v.setEnabled(true);
    for (float f : {50.0f, 440.0f, 5000.0f, 15000.0f}) {
        const auto in = sine(1024, 0.5f, f);
        std::vector<float> out(in.size());
        for (int i = 0; i < 8; ++i) v.processInterleaved(in.data(), out.data(), 1024, 2);
        ASSERT_TRUE(allFinite(out)) << f;
        for (float x : out) ASSERT_LE(std::fabs(x), 1.0f) << f;
    }
}

TEST(VolterraPackage, LegacyUpdateKernelsParityBitExact) {
    const auto h1 = defaultH1(K);
    const auto h2 = defaultH2(K);
    VolterraH2Symmetric a(K, 2), b(K, 2);
    a.setEnabled(false); b.setEnabled(false);
    a.updateKernels(h1.data(), h2.data(), K);                       // ruta legacy
    ASSERT_EQ(load(b, makePkg(), testPolicy()), S::Ok);             // ruta paquete
    ASSERT_TRUE(a.hasEffectiveKernel());
    ASSERT_TRUE(b.hasEffectiveKernel());
    a.setEnabled(true); b.setEnabled(true);
    const auto in = sine(512, 0.5f, 700.0f);
    std::vector<float> oa(in.size()), ob(in.size());
    for (int i = 0; i < 30; ++i) {
        a.processInterleaved(in.data(), oa.data(), 512, 2);
        b.processInterleaved(in.data(), ob.data(), 512, 2);
        ASSERT_EQ(std::memcmp(oa.data(), ob.data(), oa.size() * sizeof(float)), 0) << "bloque " << i;
    }
}

TEST(VolterraPackage, LegacyUpdateKernelsStillSilentlyRejectsWhenEnabledAndCheckedExplainsWhy) {
    const auto h1 = defaultH1(K);
    const auto h2 = defaultH2(K);
    VolterraH2Symmetric v(K, 2);  // habilitado
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_FALSE(v.hasEffectiveKernel());
    EXPECT_EQ(v.updateKernelsChecked(h1.data(), h2.data(), K), S::EngineBusy);
    EXPECT_EQ(v.updateKernelsChecked(h1.data(), h2.data(), K + 1), S::KernelLengthMismatch);
    EXPECT_EQ(v.updateKernelsChecked(nullptr, h2.data(), K), S::NullArgument);
    v.setEnabled(false);
    auto bad = h1; bad[0] = std::numeric_limits<float>::infinity();
    EXPECT_EQ(v.updateKernelsChecked(bad.data(), h2.data(), K), S::NonFinite);
    EXPECT_EQ(v.updateKernelsChecked(h1.data(), h2.data(), K), S::Ok);
    EXPECT_EQ(v.kernelLength(), K);
}

TEST(VolterraPackage, DefaultIdentityStateIsExactPassthrough) {
    VolterraH2Symmetric v(64, 2);  // configuración de producción (Ruta A/B)
    EXPECT_TRUE(v.isEnabled());
    EXPECT_FALSE(v.hasEffectiveKernel());
    const auto in = sine(512, 0.8f, 1234.0f);
    std::vector<float> out(in.size(), -1.0f);
    for (int i = 0; i < 10; ++i) {
        v.processInterleaved(in.data(), out.data(), 512, 2);
        ASSERT_EQ(std::memcmp(in.data(), out.data(), in.size() * sizeof(float)), 0);
    }
}
