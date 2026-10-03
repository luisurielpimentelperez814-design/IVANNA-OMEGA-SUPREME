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
