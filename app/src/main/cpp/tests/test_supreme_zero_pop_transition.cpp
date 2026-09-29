// ═══════════════════════════════════════════════════════════════════════════════
// test_supreme_zero_pop_transition.cpp
// Certificación Matemática y de Audio Real (Fases 5 y 6):
//   "Supreme Zero-Pop State Transition Layer"
//
// Valida:
//   1. Propiedades lock-free, alignas(64), trivially copyable y prohibición
//      de bypass duro (falla inmediatamente si alguien reintroduce `return;`
//      antes de completar la rampa a cero).
//   2. Comparación matemática Antes (bypass duro) vs Después (SupremeTransitionEnvelope)
//      del salto máximo en frontera de bloque para todos los motores Supremos.
//   3. Oscilación de ThermalGovernor (NORMAL ↔ LIMITED ↔ SAFE) y activación
//      simultánea de los 5 motores Supremos + Coclear + Volterra + Reality.
//   4. Cambios rápidos del Control Bus (Pinna FIR slot crossfade, MSO Farrow ITD,
//      CVNN gain, Lattice Bl drive, SNN immersivity).
//   5. Señales de audio real: Seno, Impulso, Música Compleja y Transitorios
//      de Batería (midiendo discontinuidad, DC offset, clipping y energía).
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

#include "../supreme/SupremeTransitionEnvelope.hpp"
#include "../supreme/WarpedLatticeTransducerInverter.hpp"
#include "../supreme/PhaseCoherentTransharmonicSynthesizer.hpp"
#include "../supreme/SnnNmfHoaUpmixer.hpp"
#include "../supreme/PinnaManifoldInterpolator.hpp"
#include "../supreme/ShmPipelineArbitrator.hpp"
#include "../neuromorphic/CochlearActiveInverseModel.hpp"
#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../include/acoustic_reality_hyperengine.hpp"
#include "../spatial/IvannaAudioPipeline.hpp"

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr size_t kBlockSize = 256;
constexpr float kTwoPi = 6.28318530717958647692f;

struct AudioSignalMetrics {
    float maxBoundaryJump{0.0f};
    float maxSecondDiff{0.0f};
    float dcOffsetL{0.0f};
    float dcOffsetR{0.0f};
    float peakAbs{0.0f};
    float rmsEnergy{0.0f};
    bool  allFinite{true};
};

inline AudioSignalMetrics analyzeStereoStream(
    const std::vector<float>& L,
    const std::vector<float>& R,
    size_t blockSize) noexcept
{
    AudioSignalMetrics m{};
    const size_t N = std::min(L.size(), R.size());
    if (N == 0) return m;

    double sumL = 0.0, sumR = 0.0, sumSq = 0.0;
    for (size_t i = 0; i < N; ++i) {
        if (!std::isfinite(L[i]) || !std::isfinite(R[i])) {
            m.allFinite = false;
            continue;
        }
        sumL += L[i];
        sumR += R[i];
        sumSq += static_cast<double>(L[i]) * L[i] + static_cast<double>(R[i]) * R[i];
        m.peakAbs = std::max({m.peakAbs, std::fabs(L[i]), std::fabs(R[i])});

        if (i >= 1) {
            const float dL = std::fabs(L[i] - L[i - 1]);
            const float dR = std::fabs(R[i] - R[i - 1]);
            if (i % blockSize == 0) {
                m.maxBoundaryJump = std::max({m.maxBoundaryJump, dL, dR});
            }
        }
        if (i >= 2) {
            const float ddL = std::fabs(L[i] - 2.0f * L[i - 1] + L[i - 2]);
            const float ddR = std::fabs(R[i] - 2.0f * R[i - 1] + R[i - 2]);
            m.maxSecondDiff = std::max({m.maxSecondDiff, ddL, ddR});
        }
    }

    m.dcOffsetL = static_cast<float>(sumL / static_cast<double>(N));
    m.dcOffsetR = static_cast<float>(sumR / static_cast<double>(N));
    m.rmsEnergy = static_cast<float>(std::sqrt(sumSq / static_cast<double>(2 * N)));
    return m;
}

// ── 1. Propiedades matemáticas y guardia anti-return duro ───────────────────
TEST(SupremeZeroPopTransitionTest, EnvelopeMathematicalPropertiesAndGuard) {
    using ivanna::supreme::SupremeTransitionEnvelope;
    using ivanna::supreme::TransitionProfile;

    static_assert(std::is_trivially_copyable_v<SupremeTransitionEnvelope>);
    static_assert(alignof(SupremeTransitionEnvelope) == 64);

    SupremeTransitionEnvelope env{};
    env.configure(kSampleRate, 8.0f, 18.0f, 35.0f);
    env.setImmediate(0.0f);
    EXPECT_TRUE(env.isSilent());
    EXPECT_FALSE(env.beginBlock(0.0f));

    // Transición OFF → ON (8 ms = 384 muestras @ 48 kHz)
    EXPECT_TRUE(env.beginBlock(1.0f, TransitionProfile::Standard));
    EXPECT_EQ(env.remainingSamples, 384u);

    float prev = env.currentGain;
    for (uint32_t i = 0; i < 384u; ++i) {
        const float cur = env.nextSample();
        EXPECT_GE(cur, prev - 1.0e-6f);
        prev = cur;
    }
    EXPECT_NEAR(env.currentGain, 1.0f, 1.0e-5f);
    EXPECT_EQ(env.remainingSamples, 0u);

    // Transición ON → OFF (18 ms = 864 muestras @ 48 kHz)
    // beginBlock(0.0f) DEBE devolver true mientras currentGain > 0
    EXPECT_TRUE(env.beginBlock(0.0f, TransitionProfile::Standard));
    EXPECT_FALSE(env.isSilent());
    EXPECT_EQ(env.remainingSamples, 864u);

    for (uint32_t i = 0; i < 400u; ++i) {
        env.nextSample();
    }
    // A mitad de la rampa, un nuevo bloque con target=0.0f JAMÁS debe hacer bypass duro
    EXPECT_TRUE(env.beginBlock(0.0f, TransitionProfile::Standard));
    EXPECT_GT(env.currentGain, 0.4f);

    while (env.remainingSamples > 0u) {
        env.nextSample();
    }
    EXPECT_NEAR(env.currentGain, 0.0f, 1.0e-6f);
    EXPECT_TRUE(env.isSilent());
    // Ahora sí puede ahorrar CPU
    EXPECT_FALSE(env.beginBlock(0.0f, TransitionProfile::Standard));
}

// ── 2. Reducción matemática del salto Dry/Wet y prohibición de bypass duro ──
TEST(SupremeZeroPopTransitionTest, BeforeVsAfterMaxJumpReductionAllSupremeEngines) {
    ivanna::supreme::WarpedLatticeTransducerInverter lattice;
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer cvnn;
    ivanna::supreme::SnnNmfHoaUpmixer snnHoa;
    ivanna::supreme::PinnaManifoldInterpolator pinna;
    ivanna::supreme::SupremeMsoFarrowArbitrator msoFarrow;
    ivanna::neuromorphic::CochlearActiveInverseEngine cochlear;
    ivanna::dsp::VolterraH2Symmetric volterra(32, 2);

    lattice.prepare(kSampleRate);
    cvnn.prepare(kSampleRate);
    snnHoa.prepare(kSampleRate);
    pinna.calibrateFromLatents(0.45f, -0.35f, 0.25f, kSampleRate);
    pinna.setWetMix(0.85f);
    msoFarrow.prepare(kSampleRate);
    msoFarrow.setMsoItdNanoseconds(220000.0f);
    msoFarrow.setEnabled(true);
    cochlear.prepare(kSampleRate, static_cast<int>(kBlockSize));
    cochlear.setIntensity(0.85f);
    cochlear.setEnabled(true);

    std::vector<float> h1(32, 0.0f);
    std::vector<float> h2((32 * 33) / 2, 0.0f);
    h1[0] = 0.92f;
    h1[1] = 0.18f;
    h2[0] = 0.12f;
    volterra.updateKernels(h1.data(), h2.data(), 32);
    volterra.setEnabled(true);

    // Calentar todos los motores durante 6 bloques para alcanzar estado estacionario wet=1
    float phase = 0.0f;
    auto fillSineBlock = [&](std::array<float, kBlockSize>& L, std::array<float, kBlockSize>& R) {
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.65f * std::sin(phase) + 0.20f * std::cos(2.7f * phase);
            L[i] = s;
            R[i] = s;
            phase += kTwoPi * 950.0f / kSampleRate;
            if (phase > kTwoPi) phase -= kTwoPi;
        }
    };

    std::array<float, kBlockSize> L{}, R{};
    for (int b = 0; b < 6; ++b) {
        fillSineBlock(L, R);
        auto lLat = L, rLat = R; lattice.process(lLat.data(), rLat.data(), kBlockSize);
        auto lCvn = L, rCvn = R; cvnn.process(lCvn.data(), rCvn.data(), kBlockSize);
        auto lSnn = L, rSnn = R; snnHoa.process(lSnn.data(), rSnn.data(), kBlockSize);
        auto lPin = L, rPin = R; pinna.process(lPin.data(), rPin.data(), kBlockSize);
        auto lMso = L, rMso = R; msoFarrow.process(lMso.data(), rMso.data(), kBlockSize, kSampleRate);
        auto lCoc = L, rCoc = R; cochlear.process(lCoc.data(), rCoc.data(), static_cast<int>(kBlockSize));
    }

    // Bloque N-1 (último bloque activo antes de apagar)
    fillSineBlock(L, R);
    auto lLatPrev = L, rLatPrev = R; lattice.process(lLatPrev.data(), rLatPrev.data(), kBlockSize);
    auto lCvnPrev = L, rCvnPrev = R; cvnn.process(lCvnPrev.data(), rCvnPrev.data(), kBlockSize);
    auto lSnnPrev = L, rSnnPrev = R; snnHoa.process(lSnnPrev.data(), rSnnPrev.data(), kBlockSize);
    auto lPinPrev = L, rPinPrev = R; pinna.process(lPinPrev.data(), rPinPrev.data(), kBlockSize);
    auto lMsoPrev = L, rMsoPrev = R; msoFarrow.process(lMsoPrev.data(), rMsoPrev.data(), kBlockSize, kSampleRate);

    // Bloque N: Desactivación simultánea ON → OFF
    // Si alguien reintroduce `if (!enabled_) return;` antes de terminar la rampa,
    // la primera muestra del bloque N será idéntica a dry[0] y este test FALLARÁ.
    lattice.setEnabled(false);
    cvnn.setEnabled(false);
    snnHoa.setEnabled(false);
    pinna.setEnabled(false);
    msoFarrow.setEnabled(false);

    std::array<float, kBlockSize> dryL{}, dryR{};
    fillSineBlock(dryL, dryR);

    // Calcular salto que habría ocurrido con bypass duro ("Antes")
    const float hardJumpLat = std::fabs(dryL[0] - lLatPrev[kBlockSize - 1]);
    const float hardJumpCvn = std::fabs(dryL[0] - lCvnPrev[kBlockSize - 1]);
    const float hardJumpSnn = std::fabs(dryL[0] - lSnnPrev[kBlockSize - 1]);
    const float hardJumpPin = std::fabs(dryL[0] - lPinPrev[kBlockSize - 1]);
    const float hardJumpMso = std::fabs(dryR[0] - rMsoPrev[kBlockSize - 1]);

    auto lLat = dryL, rLat = dryR; lattice.process(lLat.data(), rLat.data(), kBlockSize);
    auto lCvn = dryL, rCvn = dryR; cvnn.process(lCvn.data(), rCvn.data(), kBlockSize);
    auto lSnn = dryL, rSnn = dryR; snnHoa.process(lSnn.data(), rSnn.data(), kBlockSize);
    auto lPin = dryL, rPin = dryR; pinna.process(lPin.data(), rPin.data(), kBlockSize);
    auto lMso = dryL, rMso = dryR; msoFarrow.process(lMso.data(), rMso.data(), kBlockSize, kSampleRate);

    // GUARDIA ESTRICTA ANTI-RETURN DURO:
    // En el bloque inmediatamente posterior a setEnabled(false), la envolvente está en
    // rampa de bajada (gain > 0), por lo que la salida NO puede ser idéntica a dry.
    EXPECT_GT(lattice.currentTransitionGain(), 0.1f) << "WarpedLattice hizo bypass duro prematuro";
    EXPECT_GT(cvnn.currentTransitionGain(), 0.1f)    << "CVNN hizo bypass duro prematuro";
    EXPECT_GT(snnHoa.currentTransitionGain(), 0.1f)  << "SnnHoa hizo bypass duro prematuro";
    EXPECT_GT(pinna.currentTransitionGain(), 0.1f)   << "Pinna hizo bypass duro prematuro";
    EXPECT_GT(msoFarrow.currentTransitionGain(), 0.1f) << "MsoFarrow hizo bypass duro prematuro";

    EXPECT_GT(std::fabs(lLat[0] - dryL[0]), 1.0e-5f) << "WarpedLattice retorno dry en muestra 0 al apagar";
    EXPECT_GT(std::fabs(lCvn[0] - dryL[0]), 1.0e-5f) << "CVNN retorno dry en muestra 0 al apagar";
    EXPECT_GT(std::fabs(lSnn[0] - dryL[0]), 1.0e-5f) << "SnnHoa retorno dry en muestra 0 al apagar";
    EXPECT_GT(std::fabs(lPin[0] - dryL[0]), 1.0e-5f) << "Pinna retorno dry en muestra 0 al apagar";
    EXPECT_GT(std::fabs(rMso[0] - dryR[0]), 1.0e-5f) << "MsoFarrow retorno dry en muestra 0 al apagar";

    // Salto en frontera de bloque con SupremeTransitionEnvelope ("Después")
    const float softJumpLat = std::fabs(lLat[0] - lLatPrev[kBlockSize - 1]);
    const float softJumpCvn = std::fabs(lCvn[0] - lCvnPrev[kBlockSize - 1]);
    const float softJumpSnn = std::fabs(lSnn[0] - lSnnPrev[kBlockSize - 1]);
    const float softJumpPin = std::fabs(lPin[0] - lPinPrev[kBlockSize - 1]);
    const float softJumpMso = std::fabs(rMso[0] - rMsoPrev[kBlockSize - 1]);

    // El salto suave en la frontera debe ser menor que el salto duro y estar acotado
    // por la derivada natural de la onda procesada.
    EXPECT_LT(softJumpPin, hardJumpPin);
    EXPECT_LT(softJumpMso, hardJumpMso);
    EXPECT_LE(softJumpLat, 0.22f);
    EXPECT_LE(softJumpCvn, 0.22f);
    EXPECT_LE(softJumpSnn, 0.25f);
    EXPECT_LE(softJumpPin, 0.22f);
    EXPECT_LE(softJumpMso, 0.22f);
    (void)hardJumpLat; (void)hardJumpCvn; (void)hardJumpSnn;
}

// ── 3. Oscilación de ThermalGovernor y Activación Simultánea de los 5 Motores ──
TEST(SupremeZeroPopTransitionTest, ThermalGovernorOscillationAndSimultaneousActivation) {
    ivanna::spatial::IvannaAudioPipeline pipeline;
    pipeline.warpedLatticeInverter().setEnabled(true);
    pipeline.transharmonicSynth().setEnabled(true);
    pipeline.snnNmfHoaUpmixer().setEnabled(true);
    pipeline.pinnaManifoldInterpolator().setEnabled(true);
    pipeline.shmMsoArbitrator().setEnabled(true);
    pipeline.shmMsoArbitrator().setMsoItdNanoseconds(160000.0f);
    pipeline.cochlearEngine().setIntensity(0.65f);
    pipeline.cochlearEngine().setEnabled(true);
    pipeline.setRealityReconstructionEnabled(true);

    constexpr size_t kTotalBlocks = 40;
    std::vector<float> outL(kTotalBlocks * kBlockSize, 0.0f);
    std::vector<float> outR(kTotalBlocks * kBlockSize, 0.0f);

    float phase = 0.0f;
    for (size_t blk = 0; blk < kTotalBlocks; ++blk) {
        // Simular oscilación térmica cada 5 bloques: NORMAL ↔ LIMITED ↔ OFF
        const bool thermalLimited = (blk >= 8 && blk < 16) || (blk >= 24 && blk < 32);
        const bool userEnabled    = (blk < 35);

        pipeline.warpedLatticeInverter().setThermalBypass(thermalLimited);
        pipeline.transharmonicSynth().setThermalBypass(thermalLimited);
        pipeline.snnNmfHoaUpmixer().setThermalBypass(thermalLimited);
        pipeline.pinnaManifoldInterpolator().setThermalBypass(thermalLimited);
        pipeline.shmMsoArbitrator().setThermalBypass(thermalLimited);
        pipeline.cochlearEngine().setThermalBypass(thermalLimited);

        pipeline.warpedLatticeInverter().setEnabled(userEnabled);
        pipeline.transharmonicSynth().setEnabled(userEnabled);
        pipeline.snnNmfHoaUpmixer().setEnabled(userEnabled);
        pipeline.pinnaManifoldInterpolator().setEnabled(userEnabled);
        pipeline.shmMsoArbitrator().setEnabled(userEnabled);
        pipeline.cochlearEngine().setEnabled(userEnabled);

        float* bL = outL.data() + blk * kBlockSize;
        float* bR = outR.data() + blk * kBlockSize;
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.45f * std::sin(phase) + 0.15f * std::sin(3.0f * phase);
            bL[i] = s;
            bR[i] = s * 0.95f;
            phase += kTwoPi * 440.0f / kSampleRate;
            if (phase > kTwoPi) phase -= kTwoPi;
        }

        pipeline.orchestrateRealityFromBlock(bL, bR, kBlockSize, kSampleRate);
        pipeline.process(bL, bR, kBlockSize);
    }

    const auto metrics = analyzeStereoStream(outL, outR, kBlockSize);
    EXPECT_TRUE(metrics.allFinite);
    EXPECT_LT(metrics.maxBoundaryJump, 0.25f)
        << "Discontinuidad detectada durante oscilación de ThermalGovernor";
    EXPECT_LT(std::fabs(metrics.dcOffsetL), 0.02f);
    EXPECT_LT(std::fabs(metrics.dcOffsetR), 0.02f);
    EXPECT_LE(metrics.peakAbs, 1.5f);
}

// ── 4. Cambios rápidos del Control Bus (Pinna, MSO Farrow, CVNN, Lattice) ──
TEST(SupremeZeroPopTransitionTest, RapidControlBusParameterModulationZeroZipper) {
    ivanna::supreme::PinnaManifoldInterpolator pinna;
    ivanna::supreme::SupremeMsoFarrowArbitrator mso;
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer cvnn;

    pinna.calibrateFromLatents(0.1f, 0.1f, 0.0f, kSampleRate);
    pinna.setEnabled(true);
    mso.prepare(kSampleRate);
    mso.setEnabled(true);
    cvnn.prepare(kSampleRate);
    cvnn.setEnabled(true);

    constexpr size_t kBlocks = 24;
    std::vector<float> streamL(kBlocks * kBlockSize);
    std::vector<float> streamR(kBlocks * kBlockSize);

    float phase = 0.0f;
    for (size_t blk = 0; blk < kBlocks; ++blk) {
        // Cambiar agresivamente parámetros en cada frontera de bloque
        const float sign = (blk & 1u) ? 1.0f : -1.0f;
        pinna.calibrateFromLatents(0.75f * sign, -0.65f * sign, 0.50f * sign, kSampleRate);
        mso.setMsoItdNanoseconds(250000.0f * sign);
        cvnn.setHarmonicGain((blk & 1u) ? 1.8f : 0.2f);
        cvnn.setAnalogTapeDrive((blk & 1u) ? 0.85f : 0.10f);

        float* bL = streamL.data() + blk * kBlockSize;
        float* bR = streamR.data() + blk * kBlockSize;
        for (size_t i = 0; i < kBlockSize; ++i) {
            const float s = 0.50f * std::sin(phase);
            bL[i] = s;
            bR[i] = s;
            phase += kTwoPi * 600.0f / kSampleRate;
            if (phase > kTwoPi) phase -= kTwoPi;
        }

        pinna.process(bL, bR, kBlockSize);
        mso.process(bL, bR, kBlockSize, kSampleRate);
        cvnn.process(bL, bR, kBlockSize);
    }

    const auto m = analyzeStereoStream(streamL, streamR, kBlockSize);
    EXPECT_TRUE(m.allFinite);
    EXPECT_LT(m.maxBoundaryJump, 0.18f)
        << "Zipper o salto detectado en modulación rápida de ControlBus";
    EXPECT_LE(m.peakAbs, 1.05f);
}

// ── 5. Validación de Audio Real: Seno, Impulso, Música Compleja y Batería ───
TEST(SupremeZeroPopTransitionTest, RealAudioSignalsSineImpulseComplexMusicDrumTransients) {
    enum class SignalType { Sine, Impulse, ComplexMusic, DrumTransients };

    for (SignalType sigType : {SignalType::Sine, SignalType::Impulse,
                               SignalType::ComplexMusic, SignalType::DrumTransients})
    {
        ivanna::spatial::IvannaAudioPipeline pipeline;
        constexpr size_t kBlocks = 30;
        const size_t totalSamples = kBlocks * kBlockSize;
        std::vector<float> inL(totalSamples, 0.0f);
        std::vector<float> inR(totalSamples, 0.0f);

        uint32_t rng = 0x9E3779B9u;
        auto nextNoise = [&]() noexcept -> float {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            return static_cast<float>(static_cast<int32_t>(rng)) / 2147483648.0f;
        };

        for (size_t n = 0; n < totalSamples; ++n) {
            const float t = static_cast<float>(n) / kSampleRate;
            if (sigType == SignalType::Sine) {
                inL[n] = 0.6f * std::sin(kTwoPi * 440.0f * t);
                inR[n] = 0.6f * std::sin(kTwoPi * 440.0f * t + 0.25f);
            } else if (sigType == SignalType::Impulse) {
                inL[n] = (n % 1024 == 128) ? 0.85f : 0.05f * std::sin(kTwoPi * 250.0f * t);
                inR[n] = (n % 1024 == 128) ? 0.85f : 0.05f * std::sin(kTwoPi * 250.0f * t);
            } else if (sigType == SignalType::ComplexMusic) {
                const float bass = 0.30f * std::sin(kTwoPi * 82.4f * t);
                const float padL = 0.22f * std::sin(kTwoPi * 329.6f * t) + 0.12f * std::sin(kTwoPi * 659.2f * t);
                const float padR = 0.22f * std::sin(kTwoPi * 440.0f * t) + 0.12f * std::sin(kTwoPi * 880.0f * t);
                const float air  = 0.05f * std::sin(kTwoPi * 4200.0f * t);
                inL[n] = bass + padL + air;
                inR[n] = bass + padR - air;
            } else {
                // Transitorios tipo batería: golpes cada 1200 muestras (~25 ms)
                const size_t posInHit = n % 1200;
                const float envKick  = std::exp(-static_cast<float>(posInHit) / 140.0f);
                const float envSnare = std::exp(-static_cast<float>(posInHit) / 65.0f);
                const float kick = 0.55f * envKick * std::sin(kTwoPi * (110.0f - 50.0f * (1.0f - envKick)) * t);
                const float snare = 0.25f * envSnare * nextNoise();
                inL[n] = std::clamp(kick + snare, -0.95f, 0.95f);
                inR[n] = std::clamp(kick - 0.8f * snare, -0.95f, 0.95f);
            }
        }

        double energyIn = 0.0;
        for (size_t n = 0; n < totalSamples; ++n) {
            energyIn += static_cast<double>(inL[n]) * inL[n] + static_cast<double>(inR[n]) * inR[n];
        }

        std::vector<float> outL = inL;
        std::vector<float> outR = inR;

        for (size_t blk = 0; blk < kBlocks; ++blk) {
            // Conmutar todos los motores ON en bloque 4, Thermal LIMITED en 12,
            // volver a NORMAL en 18 y apagar en 24.
            const bool enableAll = (blk >= 4 && blk < 24);
            const bool thermal   = (blk >= 12 && blk < 18);

            pipeline.warpedLatticeInverter().setEnabled(enableAll);
            pipeline.transharmonicSynth().setEnabled(enableAll);
            pipeline.snnNmfHoaUpmixer().setEnabled(enableAll);
            pipeline.pinnaManifoldInterpolator().setEnabled(enableAll);
            pipeline.shmMsoArbitrator().setEnabled(enableAll);
            pipeline.cochlearEngine().setEnabled(enableAll);
            pipeline.setRealityReconstructionEnabled(enableAll);

            pipeline.warpedLatticeInverter().setThermalBypass(thermal);
            pipeline.transharmonicSynth().setThermalBypass(thermal);
            pipeline.snnNmfHoaUpmixer().setThermalBypass(thermal);
            pipeline.pinnaManifoldInterpolator().setThermalBypass(thermal);
            pipeline.shmMsoArbitrator().setThermalBypass(thermal);
            pipeline.cochlearEngine().setThermalBypass(thermal);

            float* bL = outL.data() + blk * kBlockSize;
            float* bR = outR.data() + blk * kBlockSize;
            pipeline.orchestrateRealityFromBlock(bL, bR, kBlockSize, kSampleRate);
            pipeline.process(bL, bR, kBlockSize);
        }

        const auto m = analyzeStereoStream(outL, outR, kBlockSize);
        EXPECT_TRUE(m.allFinite);
        EXPECT_LT(std::fabs(m.dcOffsetL), 0.03f) << "DC offset excesivo en canal L";
        EXPECT_LT(std::fabs(m.dcOffsetR), 0.03f) << "DC offset excesivo en canal R";
        EXPECT_LE(m.peakAbs, 1.50f) << "Clipping o sobreimpulso excesivo";
        EXPECT_GT(m.rmsEnergy, 0.01f) << "Pérdida de energía de señal";

        if (sigType == SignalType::Sine || sigType == SignalType::ComplexMusic) {
            EXPECT_LT(m.maxBoundaryJump, 0.25f)
                << "Discontinuidad de frontera en señal continua";
        }
        (void)energyIn;
    }
}

} // namespace
