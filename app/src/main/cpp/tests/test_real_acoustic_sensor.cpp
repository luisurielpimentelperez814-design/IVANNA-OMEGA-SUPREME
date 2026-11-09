// Pruebas del sensor acústico real (micrófono): medición por banda ERB,
// error contra objetivo, coherencia mic/referencia, robustez ante frames
// inválidos, mapeo a corrección RoomProbe y throughput.
// Todo con señales sintéticas de prueba (fixtures), no es un sensor
// simulado en producción: el sensor real se alimenta de AudioRecord.
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "spatial/RealAcousticSensor.hpp"

using namespace ivanna::spatial;
static constexpr float kSr = 48000.0f;
static constexpr float kPi = 3.14159265358979f;
static constexpr int kN = RealAcousticSensor::kFftSize;  // 8192

// Seno exactamente en el bin k (sin scalloping): f = k·sr/N.
static std::vector<float> sineBin(int k, float amp, int nSamples) {
    std::vector<float> x((size_t)nSamples);
    for (int i = 0; i < nSamples; ++i)
        x[(size_t)i] = amp * std::sin(2.0f * kPi * (float)k * (float)i / (float)kN);
    return x;
}

// Ruido blanco determinista (xorshift), amplitud rms ~amp.
static std::vector<float> noise(uint32_t seed, float amp, int nSamples) {
    std::vector<float> x((size_t)nSamples);
    uint32_t s = seed;
    for (int i = 0; i < nSamples; ++i) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        const float u = (float)(s >> 8) * (1.0f / 16777216.0f);  // [0,1)
        x[(size_t)i] = amp * (2.0f * u - 1.0f);
    }
    return x;
}

// Alimenta mic (y opcionalmente ref, en lockstep) en trozos de 1024.
static void feed(RealAcousticSensor& sen, const std::vector<float>& mic,
                 const std::vector<float>* ref = nullptr) {
    constexpr int kChunk = 1024;
    for (size_t off = 0; off < mic.size(); off += kChunk) {
        const size_t n = std::min((size_t)kChunk, mic.size() - off);
        ASSERT_TRUE(sen.feedMic(mic.data() + off, n));
        if (ref != nullptr) {
            ASSERT_TRUE(sen.pushReference(ref->data() + off, n));
        }
    }
}

// Banda ERB que contiene la frecuencia f (por centro más cercano).
static int bandNear(const RealAcousticSensor& sen, float f) {
    int best = 0;
    float bestD = 1e30f;
    for (int b = 0; b < 24; ++b) {
        const float d = std::fabs(std::log2(sen.bandCenterHz(b) / f));
        if (d < bestD) { bestD = d; best = b; }
    }
    return best;
}

TEST(RealAcousticSensor, SineMagnitudeIsCorrect) {
    // Seno amplitud 0.5 → RMS 0.5/√2 → −9.03 dBFS en su banda.
    RealAcousticSensor sen;
    sen.prepare(kSr);
    // Bin 171 = 1002.0 Hz (banda ~1 kHz), bin 14 = 82.0 Hz (banda grave),
    // bin 2000 = 11718.8 Hz (banda aguda). 12 frames ≈ 2 s.
    for (int k : {171, 14, 2000}) {
        RealAcousticSensor s;
        s.prepare(kSr);
        const float f = (float)k * kSr / (float)kN;
        auto x = sineBin(k, 0.5f, 12 * kN);
        feed(s, x);
        const SensorFrame fr = s.latestFrame();
        ASSERT_TRUE(fr.valid) << "bin " << k;
        const int b = bandNear(s, f);
        EXPECT_NEAR(fr.magnitude_db[b], -9.03f, 1.5f) << "bin " << k << " banda " << b;
        EXPECT_NEAR(fr.level_dbfs, -9.03f, 1.0f) << "bin " << k;
        // El pico está en la banda del tono.
        int peak = 0;
        for (int i = 1; i < 24; ++i)
            if (fr.magnitude_db[i] > fr.magnitude_db[peak]) peak = i;
        EXPECT_EQ(peak, b) << "bin " << k;
    }
}

TEST(RealAcousticSensor, ErrorIsMagnitudeMinusRefMinusTarget) {
    RealAcousticSensor sen;
    sen.prepare(kSr);
    // Dos tonos: 1002 Hz a 0.5 y 82 Hz a 0.25 (el grave 6 dB más bajo).
    auto x1 = sineBin(171, 0.5f, 12 * kN);
    auto x2 = sineBin(14, 0.25f, 12 * kN);
    std::vector<float> x(x1.size());
    for (size_t i = 0; i < x.size(); ++i) x[i] = x1[i] + x2[i];
    feed(sen, x);
    const SensorFrame fr = sen.latestFrame();
    ASSERT_TRUE(fr.valid);
    const float ref = sen.referenceLevelDb();
    // error = magnitud − ref − target (target por defecto: Harman 2018).
    for (int b = 0; b < 24; ++b)
        EXPECT_NEAR(fr.error_db[b], fr.magnitude_db[b] - ref - sen.targetDb(b),
                    0.05f)
            << "banda " << b;
    // El tono agudo (más fuerte) queda por encima del nivel de referencia.
    EXPECT_GT(fr.error_db[bandNear(sen, 1002.0f)], 0.0f);
    EXPECT_LT(fr.error_db[bandNear(sen, 82.0f)], fr.error_db[bandNear(sen, 1002.0f)]);
    // Con objetivo +3 dB en una banda, el error de esa banda baja 3 dB
    // respecto al objetivo anterior (Harman en esa banda, ~0 dB a 1 kHz).
    float tgt[24] = {};
    const int b1k = bandNear(sen, 1002.0f);
    const float harman1k = sen.targetDb(b1k);
    tgt[b1k] = 3.0f;
    sen.setTarget(tgt);
    feed(sen, x);  // más frames para que el suavizado vea el nuevo objetivo
    const SensorFrame fr2 = sen.latestFrame();
    ASSERT_TRUE(fr2.valid);
    EXPECT_NEAR(fr2.error_db[b1k], fr.error_db[b1k] - 3.0f + harman1k, 0.1f);
}

TEST(RealAcousticSensor, DefaultTargetIsHarman2018) {
    // El target por defecto del sensor es Harman AE/OE 2018 (no plano).
    RealAcousticSensor sen;
    sen.prepare(kSr);
    for (int b = 0; b < 24; ++b) {
        EXPECT_NEAR(sen.targetDb(b),
                    eliteTargetDbAt(EliteTargetKind::Harman2018,
                                    sen.bandCenterHz(b)),
                    0.01f)
            << "banda " << b;
    }
    // Un target personalizado sobrevive a prepare() (cambio de sr).
    float tgt[24] = {};
    tgt[5] = 4.0f;
    sen.setTarget(tgt);
    sen.prepare(kSr);
    EXPECT_FLOAT_EQ(sen.targetDb(5), 4.0f);
    // Sin target personalizado, prepare() restaura Harman.
    RealAcousticSensor sen2;
    sen2.prepare(kSr);
    EXPECT_NEAR(sen2.targetDb(5),
                eliteTargetDbAt(EliteTargetKind::Harman2018,
                                sen2.bandCenterHz(5)),
                0.01f);
}

TEST(RealAcousticSensor, EliteTargetPresetAndBassTrim) {
    // El sensor usa los mismos presets élite que el operador
    // (EliteTargetCurves): Harman 2018 por defecto, élite-lean, campo
    // difuso H&M y altavoces en sala, más control de graves ±3 dB.
    RealAcousticSensor sen;
    sen.prepare(kSr);
    EXPECT_EQ(sen.targetKind(), EliteTargetKind::Harman2018);
    EXPECT_FLOAT_EQ(sen.bassTrimDb(), 0.0f);
    // Élite-lean: la curva construida es la de los anclajes élite-lean.
    sen.setEliteTarget(EliteTargetKind::HarmanEliteLean, 0.0f);
    EXPECT_EQ(sen.targetKind(), EliteTargetKind::HarmanEliteLean);
    for (int b = 0; b < 24; ++b)
        EXPECT_NEAR(sen.targetDb(b),
                    eliteTargetDbAt(EliteTargetKind::HarmanEliteLean,
                                    sen.bandCenterHz(b)),
                    0.01f)
            << "banda " << b;
    // En graves el élite-lean está por debajo de Harman; a 1 kHz son
    // idénticos (el ajuste élite solo toca los graves).
    EXPECT_LT(sen.targetDb(0),
              eliteTargetDbAt(EliteTargetKind::Harman2018,
                              sen.bandCenterHz(0)) -
                  1.0f);
    EXPECT_NEAR(sen.targetDb(bandNear(sen, 1002.0f)),
                eliteTargetDbAt(EliteTargetKind::Harman2018,
                                sen.bandCenterHz(bandNear(sen, 1002.0f))),
                0.01f);
    // Control de graves +3 dB: el grave sube ~3 dB (estante <80 Hz con
    // desvanecimiento a 250 Hz; la banda 0, a 82 Hz, recibe ~2.9 dB) y
    // la banda de 1 kHz no se mueve.
    sen.setEliteTarget(EliteTargetKind::Harman2018, 3.0f);
    EXPECT_FLOAT_EQ(sen.bassTrimDb(), 3.0f);
    EXPECT_NEAR(sen.targetDb(0),
                eliteTargetDbAt(EliteTargetKind::Harman2018,
                                sen.bandCenterHz(0)) +
                    2.9f,
                0.15f);
    EXPECT_NEAR(sen.targetDb(bandNear(sen, 1002.0f)),
                eliteTargetDbAt(EliteTargetKind::Harman2018,
                                sen.bandCenterHz(bandNear(sen, 1002.0f))),
                0.01f);
    // setBassTrimDb re-aplica el preset vigente con el nuevo trim.
    sen.setBassTrimDb(-3.0f);
    EXPECT_EQ(sen.targetKind(), EliteTargetKind::Harman2018);
    EXPECT_FLOAT_EQ(sen.bassTrimDb(), -3.0f);
    EXPECT_NEAR(sen.targetDb(0),
                eliteTargetDbAt(EliteTargetKind::Harman2018,
                                sen.bandCenterHz(0)) -
                    2.9f,
                0.15f);
    // Kind inválido → Harman 2018 (fail-closed); trim fuera de rango
    // se recorta a ±3 dB.
    sen.setEliteTarget(static_cast<EliteTargetKind>(99), 10.0f);
    EXPECT_EQ(sen.targetKind(), EliteTargetKind::Harman2018);
    EXPECT_FLOAT_EQ(sen.bassTrimDb(), 3.0f);
    // El preset sobrevive a prepare() (cambio de sample rate).
    sen.setEliteTarget(EliteTargetKind::DiffuseFieldHM, 0.0f);
    sen.prepare(kSr);
    EXPECT_EQ(sen.targetKind(), EliteTargetKind::DiffuseFieldHM);
    EXPECT_NEAR(sen.targetDb(0),
                eliteTargetDbAt(EliteTargetKind::DiffuseFieldHM,
                                sen.bandCenterHz(0)),
                0.01f);
}

TEST(RealAcousticSensor, CoherenceSameSignalIsOne) {
    RealAcousticSensor sen;
    sen.prepare(kSr);
    auto x = sineBin(171, 0.5f, 12 * kN);
    feed(sen, x, &x);  // referencia = la misma señal
    const SensorFrame fr = sen.latestFrame();
    ASSERT_TRUE(fr.valid);
    EXPECT_GT(fr.coherence[bandNear(sen, 1002.0f)], 0.9f);
}

TEST(RealAcousticSensor, CoherenceUncorrelatedIsLow) {
    RealAcousticSensor sen;
    sen.prepare(kSr);
    auto mic = noise(0x1234567u, 0.3f, 12 * kN);
    auto ref = noise(0x89abcdefu, 0.3f, 12 * kN);
    feed(sen, mic, &ref);
    const SensorFrame fr = sen.latestFrame();
    ASSERT_TRUE(fr.valid);
    float mean = 0.0f;
    for (int b = 0; b < 24; ++b) mean += fr.coherence[b];
    mean /= 24.0f;
    EXPECT_LT(mean, 0.3f) << "coherencia media de ruidos independientes";
}

TEST(RealAcousticSensor, CoherenceWithoutReferenceIsZero) {
    RealAcousticSensor sen;
    sen.prepare(kSr);
    auto x = sineBin(171, 0.5f, 12 * kN);
    feed(sen, x);  // sin referencia
    const SensorFrame fr = sen.latestFrame();
    ASSERT_TRUE(fr.valid);
    for (int b = 0; b < 24; ++b) EXPECT_EQ(fr.coherence[b], 0.0f) << "banda " << b;
}

TEST(RealAcousticSensor, InvalidFrameDoesNotCorruptState) {
    RealAcousticSensor sen;
    sen.prepare(kSr);
    auto x = sineBin(171, 0.5f, 12 * kN);
    feed(sen, x);
    const SensorFrame good = sen.latestFrame();
    ASSERT_TRUE(good.valid);
    const uint64_t idxBefore = good.frame_index;
    // 1) Bloque todo-NaN: se rechaza, el estado no cambia.
    std::vector<float> nan(4096, std::nanf(""));
    EXPECT_FALSE(sen.feedMic(nan.data(), nan.size()));
    const SensorFrame afterNan = sen.latestFrame();
    EXPECT_EQ(afterNan.frame_index, idxBefore);
    EXPECT_TRUE(afterNan.valid);
    EXPECT_FLOAT_EQ(afterNan.magnitude_db[bandNear(sen, 1002.0f)],
                    good.magnitude_db[bandNear(sen, 1002.0f)]);
    // 2) Silencio: se publica valid=false, sin corromper el suavizado.
    //    La ventana de análisis es de 8192 muestras: hacen falta 8192
    //    muestras de silencio para que la ventana entera sea silencio
    //    (con solo 4096, la mitad de la ventana seguiría siendo el tono).
    std::vector<float> silence(8192, 0.0f);
    EXPECT_TRUE(sen.feedMic(silence.data(), silence.size()));
    const SensorFrame quiet = sen.latestFrame();
    EXPECT_FALSE(quiet.valid);
    EXPECT_LT(quiet.level_dbfs, -70.0f);
    // 3) Señal válida de nuevo: el estado se recupera con la magnitud correcta.
    feed(sen, x);
    const SensorFrame back = sen.latestFrame();
    ASSERT_TRUE(back.valid);
    EXPECT_NEAR(back.magnitude_db[bandNear(sen, 1002.0f)], -9.03f, 1.5f);
    // 4) feed vacío / nulo no hace nada ni crashea.
    EXPECT_FALSE(sen.feedMic(nullptr, 10));
    EXPECT_FALSE(sen.feedMic(x.data(), 0));
    EXPECT_FALSE(sen.pushReference(nullptr, 10));
}

TEST(RealAcousticSensor, MappingToRoomCorrection) {
    RealAcousticSensor sen;
    sen.prepare(kSr);
    // Error plano +6 dB en las 24 bandas → corrección −6 dB en las 10.
    float err[24];
    for (int b = 0; b < 24; ++b) err[b] = 6.0f;
    float corr[10];
    sen.mapErrorToRoomCorrection(err, 1.0f, corr);
    for (int b = 0; b < 10; ++b) EXPECT_NEAR(corr[b], -6.0f, 0.01f) << "banda " << b;
    // Error −10 dB → corrección +10, acotada a +3 dB.
    for (int b = 0; b < 24; ++b) err[b] = -10.0f;
    sen.mapErrorToRoomCorrection(err, 1.0f, corr);
    for (int b = 0; b < 10; ++b) EXPECT_NEAR(corr[b], 3.0f, 0.01f) << "banda " << b;
    // amount=0 → sin corrección.
    sen.mapErrorToRoomCorrection(err, 0.0f, corr);
    for (int b = 0; b < 10; ++b) EXPECT_NEAR(corr[b], 0.0f, 0.01f) << "banda " << b;
    // amount=0.5, error +6 → corrección −3 dB.
    for (int b = 0; b < 24; ++b) err[b] = 6.0f;
    sen.mapErrorToRoomCorrection(err, 0.5f, corr);
    for (int b = 0; b < 10; ++b) EXPECT_NEAR(corr[b], -3.0f, 0.01f) << "banda " << b;
    // Localidad: error solo en la banda de 1 kHz → la corrección de 1 kHz
    // es negativa y la de 31.5 Hz es ~0.
    for (int b = 0; b < 24; ++b) err[b] = 0.0f;
    err[bandNear(sen, 1000.0f)] = 6.0f;
    sen.mapErrorToRoomCorrection(err, 1.0f, corr);
    // RoomProbe: [31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000]
    EXPECT_LT(corr[5], -1.0f);  // 1000 Hz
    EXPECT_NEAR(corr[0], 0.0f, 0.01f);  // 31.5 Hz
}

TEST(RealAcousticSensor, ThroughputIsWellAboveRealtime) {
    RealAcousticSensor sen;
    sen.prepare(kSr);
    auto x = noise(0x5555u, 0.3f, 10 * 48000);  // 10 s de audio
    const auto t0 = std::chrono::steady_clock::now();
    feed(sen, x, &x);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double rt = 10.0 / sec;
    std::printf("[sensor] 10 s de audio en %.3f s → %.1f× RT\n", sec, rt);
    EXPECT_GT(rt, 10.0);
    EXPECT_TRUE(sen.latestFrame().valid);
    EXPECT_GT(sen.framesPublished(), 100u);  // ~117 análisis en 10 s
}
