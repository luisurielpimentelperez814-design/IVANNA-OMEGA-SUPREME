// ============================================================================
//  test_haptic_band_mapper.cpp — barrera real de immersion/HapticBandMapper.hpp
//
//   1. Silencio: ningún pulso.
//   2. Bombos de 60 Hz cada 0.5 s: un impacto por bombo (sin redisparos
//      mientras el bombo aún suena).
//   3. Tono de sub sostenido: rumble alto, sin impactos tras el arranque.
//   4. Ráfagas de alta frecuencia: textura sí, impacto no.
//   5. Intensidad media <= 60 % en la ventana de 1 s, incluso con bombos a
//      8 Hz + sub sostenido a escala completa.
//   6. Ganancia 0 => sin pulsos; NaN/Inf se ignoran; mapeo de amplitud válido.
// ============================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "../immersion/HapticBandMapper.hpp"

using ivanna::immersion::HapticBandMapper;
using ivanna::immersion::HapticFrame;

namespace {

constexpr float kSr = 48000.0f;
constexpr int kBlock = 384;  // 8 ms
constexpr float kPi = 3.14159265358979f;

struct Result {
    int impacts = 0, textureFrames = 0, pulses = 0;
    float maxRumble = 0.0f;
    float maxDuty = 0.0f;
    std::vector<HapticFrame> frames;
};

// Genera la señal por muestra con una función y la procesa en bloques.
template <typename F>
Result runBlocks(HapticBandMapper& m, float seconds, F&& sample) {
    Result r;
    const int blocks = static_cast<int>(seconds * kSr / kBlock);
    std::vector<float> buf(kBlock);
    std::size_t t = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < kBlock; ++i, ++t) buf[i] = sample(t);
        const HapticFrame f = m.process(buf.data(), buf.size());
        if (f.impact > 0.0f) ++r.impacts;
        if (f.texture > 0.0f) ++r.textureFrames;
        if (f.amplitude > 0) ++r.pulses;
        r.maxRumble = std::max(r.maxRumble, f.rumble);
        r.maxDuty = std::max(r.maxDuty, m.dutyFraction());
        r.frames.push_back(f);
    }
    return r;
}

HapticBandMapper make() {
    HapticBandMapper m;
    m.prepare(kSr, kBlock);
    return m;
}

float kick(std::size_t t, float periodSec, float amp) {
    const float ts = float(t) / kSr;
    const float ph = std::fmod(ts, periodSec);
    return amp * std::sin(2.0f * kPi * 60.0f * ph) * std::exp(-ph / 0.06f);
}

}  // namespace

TEST(HapticBandMapper, SilenceProducesNoPulses) {
    auto m = make();
    const Result r = runBlocks(m, 2.0f, [](std::size_t) { return 0.0f; });
    EXPECT_EQ(r.pulses, 0);
    EXPECT_EQ(r.impacts, 0);
}

TEST(HapticBandMapper, OneImpactPerKick) {
    auto m = make();
    // 6 bombos en 3 s, tras 0.25 s de silencio para que arranque la envolvente
    const Result r = runBlocks(m, 3.25f, [](std::size_t t) {
        const float ts = float(t) / kSr;
        return ts < 0.25f ? 0.0f : kick(t - static_cast<std::size_t>(0.25f * kSr), 0.5f, 0.8f);
    });
    EXPECT_GE(r.impacts, 6);
    EXPECT_LE(r.impacts, 7) << "no debe redispararse mientras el bombo aún suena";
}

TEST(HapticBandMapper, SustainedSubGivesRumbleNotImpacts) {
    auto m = make();
    const Result r = runBlocks(m, 4.0f, [](std::size_t t) {
        return 0.5f * std::sin(2.0f * kPi * 50.0f * float(t) / kSr);
    });
    EXPECT_GT(r.maxRumble, 0.2f);
    EXPECT_LE(r.impacts, 2) << "solo el arranque puede disparar";
    // Pasado el arranque, ninguno
    int late = 0;
    for (std::size_t i = r.frames.size() / 2; i < r.frames.size(); ++i) late += (r.frames[i].impact > 0.0f);
    EXPECT_EQ(late, 0);
}

TEST(HapticBandMapper, HighFrequencyBurstsGiveTextureNotImpact) {
    auto m = make();
    uint32_t s = 5u;
    const Result r = runBlocks(m, 3.0f, [&](std::size_t t) {
        const float ts = float(t) / kSr;
        const float ph = std::fmod(ts, 0.25f);
        s = s * 1664525u + 1013904223u;
        const float noise = (float(s >> 8) / 16777216.0f) * 2.f - 1.f;
        // 6 kHz con ráfagas cortas de 6 ms
        return (ph < 0.006f) ? 0.7f * std::sin(2.0f * kPi * 6000.0f * ts) * (0.6f + 0.4f * noise) : 0.0f;
    });
    EXPECT_GT(r.textureFrames, 4);
    EXPECT_EQ(r.impacts, 0);
}

TEST(HapticBandMapper, MeanIntensityNeverExceedsSixtyPercent) {
    auto m = make();
    const Result r = runBlocks(m, 6.0f, [](std::size_t t) {
        // Bombos a 8 Hz a escala completa + sub sostenido: el peor caso para el motor
        return std::clamp(kick(t, 0.125f, 1.0f) + 0.6f * std::sin(2.0f * kPi * 45.0f * float(t) / kSr), -1.0f, 1.0f);
    });
    EXPECT_LE(r.maxDuty, HapticBandMapper::kMaxDuty + 1e-3f);
    EXPECT_GT(r.pulses, 20) << "el límite escala, no apaga";
}

TEST(HapticBandMapper, GainZeroAndNonFiniteAreSafe) {
    auto m = make();
    m.setGain(0.0f);
    const Result quiet = runBlocks(m, 1.0f, [](std::size_t t) { return kick(t, 0.25f, 0.9f); });
    EXPECT_EQ(quiet.pulses, 0);

    auto m2 = make();
    std::vector<float> bad(kBlock, std::numeric_limits<float>::quiet_NaN());
    HapticFrame f = m2.process(bad.data(), bad.size());
    EXPECT_EQ(f.amplitude, 0);
    for (int i = 0; i < 20; ++i) f = m2.process(bad.data(), bad.size());
    const Result after = runBlocks(m2, 2.0f, [](std::size_t t) { return kick(t, 0.5f, 0.8f); });
    EXPECT_GE(after.impacts, 3) << "el estado no se envenenó";
    for (const auto& fr : after.frames) {
        if (fr.amplitude > 0) {
            EXPECT_GE(fr.amplitude, 1);
            EXPECT_TRUE(fr.durationMs == 12 || fr.durationMs == 40);
        }
    }
}
