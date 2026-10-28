#include <gtest/gtest.h>
#include "../IvannaTinyMLKernel.hpp"
#include <vector>
#include <chrono>
#include <thread>
#include <cmath>

using namespace ivanna::tinyml;

TEST(IvannaTinyMLKernelTest, AlignmentAndSizeConstraints) {
    EXPECT_EQ(alignof(IvannaTinyMLAudioKernel), 64);
    EXPECT_EQ(alignof(AcousticControlVector), 64);
}

TEST(IvannaTinyMLKernelTest, IngestionAndTripleBufferingWaitFree) {
    IvannaTinyMLAudioKernel kernel;

    // Buffer de prueba (tono senoidal a 440 Hz)
    constexpr size_t numFrames = 256;
    std::vector<float> left(numFrames);
    std::vector<float> right(numFrames);

    for (size_t i = 0; i < numFrames; ++i) {
        float sample = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);
        left[i] = sample;
        right[i] = sample;
    }

    // Ingestar varios bloques simulando el audio thread caliente
    for (int b = 0; b < 10; ++b) {
        kernel.ingestPcmBlock(left.data(), right.data(), numFrames);
    }

    // Permitir al hilo de inferencia asíncrono procesar al menos un salto de ventana
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Lectura wait-free desde el callback de audio
    AcousticControlVector ctrl;
    kernel.getAcousticControl(ctrl);

    // Debe ser válido tras la inferencia
    EXPECT_TRUE(ctrl.isValid);
    EXPECT_GE(ctrl.confidence, 0.0f);
    EXPECT_LE(ctrl.confidence, 1.0f);
    EXPECT_GE(ctrl.targetWidenerMultiplier, 0.5f);
    EXPECT_LE(ctrl.targetWidenerMultiplier, 2.0f);
    EXPECT_GE(ctrl.antiDolbyIntensity, 0.0f);
    EXPECT_LE(ctrl.antiDolbyIntensity, 1.0f);
}

TEST(IvannaTinyMLKernelTest, RingOverflowCounterStaysZeroUnderNormalLoad) {
    IvannaTinyMLAudioKernel kernel;
    constexpr size_t numFrames = 256;
    std::vector<float> left(numFrames, 0.1f);
    std::vector<float> right(numFrames, 0.1f);

    // Light load, same shape as a real audio callback: the inference worker
    // (170-sample hops every 2ms when available) drains this trivially.
    for (int b = 0; b < 10; ++b) {
        kernel.ingestPcmBlock(left.data(), right.data(), numFrames);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    EXPECT_EQ(kernel.ringOverflowCount(), 0u)
        << "normal ingestion rate must never overflow the 16384-sample ring";
}

TEST(IvannaTinyMLKernelTest, RingOverflowIsCountedNotSilentlyDropped) {
    IvannaTinyMLAudioKernel kernel;

    // A single synchronous call producing far more decimated samples
    // (numFrames / kDecimateFactor) than kRingCapacity (16384) guarantees
    // push() returns false at least once before the background inference
    // worker (170-sample hops, 2ms cadence) can meaningfully drain it --
    // this must be observable via ringOverflowCount(), not a silent no-op
    // the caller has no way to detect.
    constexpr size_t numFrames = 400'000; // decimated ~133k samples, 8x capacity
    std::vector<float> left(numFrames, 0.2f);
    std::vector<float> right(numFrames, 0.2f);

    kernel.ingestPcmBlock(left.data(), right.data(), numFrames);

    EXPECT_GT(kernel.ringOverflowCount(), 0u)
        << "pushing 8x ring capacity in one synchronous call must register "
           "at least one counted overflow, not be silently discarded";

    // The kernel must stay in a valid, crash-free state after an overflow:
    // subsequent ingestion and control reads still behave normally.
    kernel.ingestPcmBlock(left.data(), right.data(), 256);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    AcousticControlVector ctrl;
    kernel.getAcousticControl(ctrl);
    EXPECT_TRUE(std::isfinite(ctrl.confidence));
}

TEST(IvannaTinyMLKernelTest, RobustnessWithZerosAndSilence) {
    IvannaTinyMLAudioKernel kernel;

    constexpr size_t numFrames = 512;
    std::vector<float> left(numFrames, 0.0f);
    std::vector<float> right(numFrames, 0.0f);

    for (int b = 0; b < 5; ++b) {
        kernel.ingestPcmBlock(left.data(), right.data(), numFrames);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    AcousticControlVector ctrl;
    kernel.getAcousticControl(ctrl);
    // Verificamos que no produzca NaN ni Inf
    EXPECT_TRUE(std::isfinite(ctrl.confidence));
    EXPECT_TRUE(std::isfinite(ctrl.targetWidenerMultiplier));
    EXPECT_TRUE(std::isfinite(ctrl.targetVocalPresenceDb));
    EXPECT_TRUE(std::isfinite(ctrl.targetSubExciterDrive));
}
