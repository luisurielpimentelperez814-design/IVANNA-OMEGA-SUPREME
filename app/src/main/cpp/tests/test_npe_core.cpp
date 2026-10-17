// NpeStereoCore: el motor NPE neuromórfico en la Ruta B. Pruebas del contrato: apagado bit-exacto, efecto real medible
// cuando está encendido (las neuronas LIF disparan y la señal cambia), seguridad numérica y encendido sin clic.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "neuromorphic/NpeStereoCore.hpp"

using ivanna::neuromorphic::NpeStereoCore;
namespace {
constexpr float kSr = 48000.f;
std::vector<float> music(size_t n) {          // grave + medio + agudo con ataques (≈ música)
    std::vector<float> x(n);
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / kSr;
        const float env = 0.5f + 0.5f * std::sin(2.f * 3.14159265f * 2.f * t);
        x[i] = 0.25f * env * std::sin(2.f * 3.14159265f * 110.f * t) + 0.12f * std::sin(2.f * 3.14159265f * 1500.f * t)
             + 0.05f * std::sin(2.f * 3.14159265f * 6000.f * t);
    }
    return x;
}
void run(NpeStereoCore& c, std::vector<float>& L, std::vector<float>& R) {
    for (size_t k = 0; k + 1024 <= L.size(); k += 1024) c.process(L.data() + k, R.data() + k, 1024);
}
}  // namespace

TEST(NpeStereoCore, OffIsBitExact) {
    NpeStereoCore c; c.prepare(kSr);
    auto L = music(48000), R = L; const auto L0 = L, R0 = R;
    c.setParams(false, 0.5f, 0.2f, 0.3f, 0.f);
    run(c, L, R);
    EXPECT_EQ(L, L0); EXPECT_EQ(R, R0);
    EXPECT_FALSE(c.isActive());
}

TEST(NpeStereoCore, OnHasRealMeasurableEffectAndNeuronsFire) {
    NpeStereoCore c; c.prepare(kSr);
    auto L = music(96 * 1024 / 2), R = L; const auto L0 = L;
    c.setParams(true, 0.6f, 0.3f, 0.3f, 0.f);
    int spikes = 0; double diff = 0, ref = 0;
    for (size_t k = 0; k + 1024 <= L.size(); k += 1024) { c.process(L.data() + k, R.data() + k, 1024); spikes += c.lastSpikes(); }
    for (size_t i = L.size() / 2; i < L.size(); ++i) { diff += double(L[i] - L0[i]) * (L[i] - L0[i]); ref += double(L0[i]) * L0[i]; }
    EXPECT_GT(spikes, 50);                                         // la red LIF realmente dispara
    EXPECT_GT(10.0 * std::log10(diff / ref), -30.0);               // el residuo es audible (> -30 dB de la señal)
    EXPECT_TRUE(c.isActive());
}

TEST(NpeStereoCore, OutputIsFiniteAndBoundedWithHotInput) {
    NpeStereoCore c; c.prepare(kSr);
    std::vector<float> L(48000), R(48000);
    for (size_t i = 0; i < L.size(); ++i) { L[i] = (i % 97 < 48) ? 1.5f : -1.5f; R[i] = -L[i]; }   // cuadrada fuera de escala
    c.setParams(true, 2.0f, 1.0f, 1.0f, 12.f);
    run(c, L, R);
    for (size_t i = 0; i < L.size(); ++i) { ASSERT_TRUE(std::isfinite(L[i]) && std::isfinite(R[i])); }
    float pk = 0; for (size_t i = 40000; i < L.size(); ++i) pk = std::max(pk, std::fabs(L[i]));
    EXPECT_LE(pk, 1.6f);                                           // no amplifica más que la entrada fuera de escala
}

TEST(NpeStereoCore, SwitchingOnIsClickFree) {
    NpeStereoCore c; c.prepare(kSr);
    auto L = music(48000), R = L; const auto L0 = L;
    c.setParams(false, 0.5f, 0.2f, 0.3f, 0.f);
    c.process(L.data(), R.data(), 1024);
    c.setParams(true, 0.5f, 0.2f, 0.3f, 0.f);
    c.process(L.data() + 1024, R.data() + 1024, 1024);
    float maxJump = 0;                                             // salto muestra a muestra frente al original
    for (int i = 1024; i < 2048; ++i) maxJump = std::max(maxJump, std::fabs((L[i] - L0[i]) - (L[i - 1] - L0[i - 1])));
    EXPECT_LT(maxJump, 0.02f);                                     // fundido de 20 ms, sin escalón
}
