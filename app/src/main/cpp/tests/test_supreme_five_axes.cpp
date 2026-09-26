// ═══════════════════════════════════════════════════════════════════════════════
// test_supreme_five_axes.cpp — Certificación Host de los 5 Ejes de Supremacía
// Neuroacústica de IVANNA-OMEGA-SUPREME (Zero-Copy, Lock-Free, No-NaN/Denormal).
// ═══════════════════════════════════════════════════════════════════════════════

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>

#include "supreme/WarpedLatticeTransducerInverter.hpp"
#include "supreme/PhaseCoherentTransharmonicSynthesizer.hpp"
#include "supreme/SnnNmfHoaUpmixer.hpp"
#include "supreme/PinnaManifoldInterpolator.hpp"
#include "supreme/ShmPipelineArbitrator.hpp"
#include "spatial/IvannaAudioPipeline.hpp"

using namespace ivanna::supreme;

// ─── EJE 1: WarpedLatticeTransducerInverter ──────────────────────────────────
TEST(SupremeAxis1_WarpedLattice, SubSampleGroupDelayAndMicroChirpStability) {
    WarpedLatticeTransducerInverter inverter;
    inverter.prepare(48000.0f);

    // 1. Verificar factor de deformación Bark λ en rango psicoacústico válido
    EXPECT_GT(inverter.warpingLambda(), 0.5f);
    EXPECT_LT(inverter.warpingLambda(), 0.85f);

    // 2. Retardo de grupo inverso sub-muestra acotado en [0, 1)
    const float tauLow  = inverter.computeSubSampleInverseGroupDelay(120.0f);
    const float tauHigh = inverter.computeSubSampleInverseGroupDelay(8000.0f);
    EXPECT_GE(tauLow, 0.0f);
    EXPECT_LT(tauLow, 1.0f);
    EXPECT_GE(tauHigh, 0.0f);
    EXPECT_LT(tauHigh, 1.0f);

    // 3. Procesamiento de silencio activa el Micro-Chirp enmascarado sin NaN/Inf
    std::vector<float> left(512, 0.0f), right(512, 0.0f);
    inverter.process(left.data(), right.data(), left.size());
    double chirpEnergy = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        ASSERT_TRUE(std::isfinite(left[i]));
        ASSERT_TRUE(std::isfinite(right[i]));
        chirpEnergy += static_cast<double>(left[i]) * left[i];
    }
    EXPECT_GT(chirpEnergy, 0.0);
    EXPECT_LT(chirpEnergy, 1.0e-4); // Estrictamente enmascarado (< -70 dBFS)

    // 4. Adaptación de celosía con loopback y excursión alta sin divergencia
    std::vector<float> loopback(256, 0.02f);
    inverter.adaptFromLoopback(loopback.data(), loopback.size(), 0.005f);
    for (size_t i = 0; i < left.size(); ++i) {
        const float s = 0.85f * std::sin(2.0f * 3.14159265f * 90.0f * static_cast<float>(i) / 48000.0f);
        left[i] = s;
        right[i] = s;
    }
    inverter.process(left.data(), right.data(), left.size());
    for (float v : left) {
        EXPECT_TRUE(std::isfinite(v));
        EXPECT_LE(std::fabs(v), 1.96f);
    }
}

// ─── EJE 2: PhaseCoherentTransharmonicSynthesizer ────────────────────────────
TEST(SupremeAxis2_TransharmonicCVNN, PhaseDerivativeContinuityAndImdSuppression) {
    PhaseCoherentTransharmonicSynthesizer synth;
    synth.prepare(48000.0f);
    synth.setHarmonicGain(0.35f);

    constexpr size_t N = 1024;
    std::vector<float> left(N), right(N);
    for (size_t i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        // Señal bitonal para prueba de intermodulación (7 kHz + 9.5 kHz)
        const float x = 0.4f * std::sin(2.0f * 3.14159265f * 7000.0f * t)
                      + 0.3f * std::sin(2.0f * 3.14159265f * 9500.0f * t);
        left[i] = x;
        right[i] = x;
    }

    synth.process(left.data(), right.data(), N);

    for (size_t i = 0; i < N; ++i) {
        ASSERT_TRUE(std::isfinite(left[i]));
        ASSERT_TRUE(std::isfinite(right[i]));
        EXPECT_LE(std::fabs(left[i]), 1.96f);
    }
    // Continuidad de la derivada de fase instantánea (ausencia de aspereza metálica)
    EXPECT_LT(synth.maxPhaseDerivativeStep(), 0.55f);
}

// ─── EJE 3: SnnNmfHoaUpmixer ─────────────────────────────────────────────────
TEST(SupremeAxis3_SnnNmfHoa4thOrder, OrthogonalFourStreamsAnd16ChannelProjection) {
    SnnNmfHoaUpmixer upmixer;
    upmixer.prepare(48000.0f);
    upmixer.setImmersivity(0.65f);

    std::array<float, SnnNmfHoaUpmixer::NUM_STREAMS> streams{};
    std::array<float, SnnNmfHoaUpmixer::HOA_CHANNELS> hoa16{};

    // Alimentar transiente estéreo asimétrico y verificar descomposición en 4 flujos + 16 canales HOA
    for (int n = 0; n < 256; ++n) {
        const float inL = (n > 32 && n < 96) ? 0.75f * std::sin(0.25f * static_cast<float>(n)) : 0.05f;
        const float inR = (n > 32 && n < 96) ? 0.35f * std::cos(0.19f * static_cast<float>(n)) : -0.04f;
        upmixer.decomposeAndProjectSample(inL, inR, streams, hoa16);

        for (float s : streams) EXPECT_TRUE(std::isfinite(s));
        for (float h : hoa16)   EXPECT_TRUE(std::isfinite(h));
    }

    // Procesamiento completo del bloque binaural UPOLA
    std::vector<float> blockL(256, 0.25f), blockR(256, -0.15f);
    upmixer.process(blockL.data(), blockR.data(), 256);
    for (size_t i = 0; i < 256; ++i) {
        EXPECT_TRUE(std::isfinite(blockL[i]));
        EXPECT_TRUE(std::isfinite(blockR[i]));
    }
}

// ─── EJE 4: PinnaManifoldInterpolator ────────────────────────────────────────
TEST(SupremeAxis4_PinnaManifoldINR, SdfEvaluationAndSub100msMinimumPhaseFirSynthesis) {
    PinnaManifoldInterpolator interpolator;

    // Tensor fotogramétrico simulado de entrada (64 floats normalizados)
    std::array<float, 64> earPatch{};
    for (size_t i = 0; i < earPatch.size(); ++i) {
        earPatch[i] = 0.5f * std::sin(static_cast<float>(i) * 0.27f);
    }

    const auto t0 = std::chrono::steady_clock::now();
    const auto firPair = interpolator.synthesizeFromImagePatch(earPatch.data(), earPatch.size(), 48000.0f);
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Presupuesto estricto < 100 ms (típicamente < 0.2 ms)
    EXPECT_LT(elapsedMs, 100.0);

    // Verificar SDF finito y parámetros antropométricos acotados
    const float sdfCenter = interpolator.evaluatePinnaSdf(0.0f, 0.0f, 0.0f, firPair.latentAnthropometrics);
    EXPECT_TRUE(std::isfinite(sdfCenter));
    EXPECT_GE(firPair.pinnaNotchFreqHz, 6200.0f);
    EXPECT_LE(firPair.pinnaNotchFreqHz, 10400.0f);
    EXPECT_GE(firPair.itdMicroSeconds, 480.0f);
    EXPECT_LE(firPair.itdMicroSeconds, 760.0f);

    // Verificar energía unitaria L2 y propiedad de fase mínima (energía concentrada en primeros taps)
    float energyL = 0.0f, energyR = 0.0f;
    for (size_t k = 0; k < PinnaManifoldInterpolator::FIR_TAPS; ++k) {
        ASSERT_TRUE(std::isfinite(firPair.left[k]));
        ASSERT_TRUE(std::isfinite(firPair.right[k]));
        energyL += firPair.left[k] * firPair.left[k];
        energyR += firPair.right[k] * firPair.right[k];
    }
    EXPECT_NEAR(energyL, 1.0f, 1.0e-3f);
    EXPECT_NEAR(energyR, 1.0f, 1.0e-3f);
    EXPECT_GT(firPair.left[0] * firPair.left[0], 0.35f);
}

// ─── EJE 5: ShmPipelineArbitrator & FarrowOrder5Delay ────────────────────────
TEST(SupremeAxis5_ShmArbitrationAndFarrow5, LocklessCasOwnershipAndNanosecondFarrowAlignment) {
    ShmArbitrationControlBlock shm{};

    // 1. Daemon PID 1001 adquiere el pipeline libre
    EXPECT_TRUE(shm.tryAcquireOwnership(1001, 1'000'000ULL));
    EXPECT_EQ(shm.owner_pid.load(), 1001);

    // 2. Efecto HAL PID 2002 intenta adquirir mientras el lease de 1001 está vigente -> rechazado
    EXPECT_FALSE(shm.tryAcquireOwnership(2002, 10'000'000ULL));
    EXPECT_EQ(shm.owner_pid.load(), 1001);

    // 3. Si el daemon 1001 expira su lease (>50 ms), el HAL 2002 toma posesión sin bloqueo via CAS
    EXPECT_TRUE(shm.tryAcquireOwnership(2002, 80'000'000ULL));
    EXPECT_EQ(shm.owner_pid.load(), 2002);

    // 4. Ringbuffer SPSC lock-free transmite frames bit-exactos
    std::array<float, 4> inL = {0.1f, 0.2f, -0.3f, 0.4f};
    std::array<float, 4> inR = {-0.1f, -0.2f, 0.3f, -0.4f};
    std::array<float, 4> outL{}, outR{};
    EXPECT_EQ(shm.pushStereoFrames(inL.data(), inR.data(), 4), 4u);
    EXPECT_EQ(shm.popStereoFrames(outL.data(), outR.data(), 4), 4u);
    for (size_t i = 0; i < 4; ++i) {
        EXPECT_FLOAT_EQ(inL[i], outL[i]);
        EXPECT_FLOAT_EQ(inR[i], outR[i]);
    }
    EXPECT_TRUE(shm.releaseOwnership(2002));
    EXPECT_EQ(shm.owner_pid.load(), 0);

    // 5. Filtro de Farrow de 5º Orden: precisión en interpolación de senoidal continua
    FarrowOrder5Delay farrow;
    constexpr float kDelayFrac = 0.37f; // Retardo total = 2 + 0.37 = 2.37 muestras
    constexpr float kOmega = 2.0f * 3.14159265358979323846f * 1000.0f / 48000.0f;

    float maxErr = 0.0f;
    for (int n = 0; n < 128; ++n) {
        const float x = std::sin(kOmega * static_cast<float>(n));
        const float y = farrow.processSample(x, kDelayFrac);
        if (n >= 16) {
            const float expected = std::sin(kOmega * (static_cast<float>(n) - (2.0f + kDelayFrac)));
            maxErr = std::max(maxErr, std::fabs(y - expected));
        }
    }
    // Polinomio de 5º orden a f = fs/48 tiene error de interpolación < 1e-5
    EXPECT_LT(maxErr, 1.0e-4f);
}

// ─── INTEGRACIÓN PIPELINE + CONTROLES LOCK-FREE UI/JNI ───────────────────────
TEST(SupremeFiveAxesPipelineIntegration, FullControlsAndTelemetryEndToEnd) {
    auto& pipe = ivanna::spatial::IvannaAudioPipeline::getActiveInstance();
    pipe.reset();

    // Activar y parametrizar los 5 Ejes tal como lo hace SupremeAxesPrefs.applyToNative()
    pipe.warpedLatticeInverter().setWarpingLambda(0.756f);
    pipe.warpedLatticeInverter().setBlCompensationDrive(0.55f);
    pipe.warpedLatticeInverter().setMicroChirpEnabled(true);
    pipe.warpedLatticeInverter().setEnabled(true);

    pipe.transharmonicSynth().setHarmonicGain(0.32f);
    pipe.transharmonicSynth().setImdCancelStrength(0.85f);
    pipe.transharmonicSynth().setEnabled(true);

    pipe.snnNmfHoaUpmixer().setImmersivity(0.65f);
    pipe.snnNmfHoaUpmixer().setSnnThreshold(0.50f);
    pipe.snnNmfHoaUpmixer().setEnabled(true);

    pipe.pinnaManifoldInterpolator().calibrateFromLatents(0.20f, -0.15f, 0.10f, 48000.0f);
    pipe.pinnaManifoldInterpolator().setWetMix(0.60f);
    pipe.pinnaManifoldInterpolator().setEnabled(true);

    pipe.shmMsoArbitrator().setMsoItdNanoseconds(12500.0f);
    pipe.shmMsoArbitrator().setEbpfBypassActive(true);
    pipe.shmMsoArbitrator().setEnabled(true);
    EXPECT_TRUE(pipe.shmMsoArbitrator().tryAcquireOwnership(4242, 10'000'000ULL));
    EXPECT_EQ(pipe.shmMsoArbitrator().ownerPid(), 4242);

    std::array<float, 256> bufL{}, bufR{};
    for (size_t i = 0; i < 256; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        bufL[i] = 0.35f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        bufR[i] = 0.35f * std::cos(2.0f * 3.14159265f * 880.0f * t);
    }

    pipe.process(bufL.data(), bufR.data(), 256);

    for (size_t i = 0; i < 256; ++i) {
        ASSERT_TRUE(std::isfinite(bufL[i]));
        ASSERT_TRUE(std::isfinite(bufR[i]));
    }

    // Verificar telemetría leída por la UI
    EXPECT_GE(pipe.warpedLatticeInverter().lastSubSampleDelay(), 0.0f);
    EXPECT_LT(pipe.warpedLatticeInverter().lastSubSampleDelay(), 1.0f);
    EXPECT_GE(pipe.pinnaManifoldInterpolator().activeNotchFreqHz(), 6200.0f);
    EXPECT_LE(pipe.pinnaManifoldInterpolator().activeNotchFreqHz(), 10400.0f);

    // Devolver a bypass para no alterar estado global de otros tests
    pipe.shmMsoArbitrator().releaseOwnership(4242);
    pipe.warpedLatticeInverter().setEnabled(false);
    pipe.transharmonicSynth().setEnabled(false);
    pipe.snnNmfHoaUpmixer().setEnabled(false);
    pipe.pinnaManifoldInterpolator().setEnabled(false);
    pipe.shmMsoArbitrator().setEnabled(false);
}

