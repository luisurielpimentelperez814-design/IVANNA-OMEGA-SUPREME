// test_stage_timings.cpp — Tiempos por etapa del DSP (v2.5.411, tarea 4).
//
// Verifica el recorder lock-free de ivanna_stage_timings.h: media, p99 y
// máximo por etapa, independencia entre etapas, y que el snapshot-and-reset
// de la ventana de 1 s deja la siguiente ventana en cero. No mide audio:
// mide la aritmética del instrumento (el p99 exacto del anillo).
// La instancia g_stageTimings viene de ../ivanna_route_a_asrc.cpp (donde
// vive la definición única, compartida con el JNI).
#include <gtest/gtest.h>

#include <cmath>

#include "../include/ivanna_stage_timings.h"

using namespace ivanna;

namespace {

// El snapshot resetea: cada test empieza vaciando la ventana anterior.
void drain() {
    float out[kStageCount * 3];
    stageSnapshotReset(out);
}

float* statsOf(float* out, Stage s) { return &out[static_cast<int>(s) * 3]; }

} // namespace

TEST(StageTimings, MeanP99MaxOfHundredSamples) {
    drain();
    for (int i = 1; i <= 100; ++i)
        stageRecord(Stage::Hoa, static_cast<uint64_t>(i) * 1000); // 1..100 us en ns
    float out[kStageCount * 3];
    stageSnapshotReset(out);
    const float* hoa = statsOf(out, Stage::Hoa);
    EXPECT_FLOAT_EQ(hoa[0], 50500.0f);  // media 1..100 us
    EXPECT_FLOAT_EQ(hoa[1], 99000.0f);  // p99 = muestra 99
    EXPECT_FLOAT_EQ(hoa[2], 100000.0f); // max = muestra 100
}

TEST(StageTimings, SnapshotResetEmptiesNextWindow) {
    drain();
    stageRecord(Stage::Orbe, 5000);
    float out[kStageCount * 3];
    stageSnapshotReset(out);
    EXPECT_FLOAT_EQ(statsOf(out, Stage::Orbe)[0], 5000.0f);
    stageSnapshotReset(out);
    for (int i = 0; i < kStageCount * 3; ++i) EXPECT_FLOAT_EQ(out[i], 0.0f);
}

TEST(StageTimings, StagesAreIndependent) {
    drain();
    stageRecord(Stage::Holos, 1000);
    stageRecord(Stage::Holos, 3000);
    stageRecord(Stage::Phi, 7000);
    float out[kStageCount * 3];
    stageSnapshotReset(out);
    const float* holos = statsOf(out, Stage::Holos);
    EXPECT_FLOAT_EQ(holos[0], 2000.0f);
    EXPECT_FLOAT_EQ(holos[1], 3000.0f);
    EXPECT_FLOAT_EQ(holos[2], 3000.0f);
    const float* phi = statsOf(out, Stage::Phi);
    EXPECT_FLOAT_EQ(phi[0], 7000.0f);
    EXPECT_FLOAT_EQ(phi[1], 7000.0f);
    EXPECT_FLOAT_EQ(phi[2], 7000.0f);
    // Las demás etapas no recibieron muestras: cero, no basura.
    EXPECT_FLOAT_EQ(statsOf(out, Stage::Volterra)[0], 0.0f);
    EXPECT_FLOAT_EQ(statsOf(out, Stage::Npe)[2], 0.0f);
    EXPECT_FLOAT_EQ(statsOf(out, Stage::Asrc)[1], 0.0f);
}

TEST(StageTimings, SingleSampleMeanP99MaxAreEqual) {
    drain();
    stageRecord(Stage::Asrc, 123456);
    float out[kStageCount * 3];
    stageSnapshotReset(out);
    const float* asrc = statsOf(out, Stage::Asrc);
    EXPECT_FLOAT_EQ(asrc[0], 123456.0f);
    EXPECT_FLOAT_EQ(asrc[1], 123456.0f);
    EXPECT_FLOAT_EQ(asrc[2], 123456.0f);
}

TEST(StageTimings, EmptyWindowIsAllZeros) {
    drain();
    float out[kStageCount * 3];
    stageSnapshotReset(out);
    for (int i = 0; i < kStageCount * 3; ++i) EXPECT_FLOAT_EQ(out[i], 0.0f);
}
