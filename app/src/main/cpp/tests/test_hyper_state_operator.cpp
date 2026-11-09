// test_hyper_state_operator.cpp — Hiper-Operador de Estado Φ v2.5.369.
// Muestra a muestra, sin FFT. Estado vivo = medición real (SensorFrame
// del micrófono) + NPE on-device; EEG y EMF reales como extensiones.
// Sin SensorFrame, el operador trabaja en modo APROXIMADO (balance
// espectral medido por el NPE) — audible en Ruta A HOY — y el modo
// MEDIDO lo reemplaza cuando llegan frames del micrófono.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <gtest/gtest.h>

// kalmanInit() resetea el Kalman GLOBAL de phase_oracle.cpp (el
// detector de transitorios que comparte BiquadEnvelopeBank con todo el
// NPE). Sin reset, dos instancias del operador en el mismo proceso no
// son bit a bit idénticas (el estado global arrastra ~1e-8). No está
// declarado en ningún header: se declara aquí (el test enlaza
// phase_oracle.cpp, como test_npe_core).
void kalmanInit();

#include "../neuromorphic/neuro_acoustic_closed_loop.hpp"
#include "../spatial/HyperStateOperator.hpp"

namespace {

using ivanna::spatial::EliteTargetKind;
using ivanna::spatial::eliteErbCenters24;
using ivanna::spatial::eliteTargetDbAt;
using ivanna::spatial::HyperMode;
using ivanna::spatial::HyperRoutePreset;
using ivanna::spatial::HyperStateOperator;
using ivanna::spatial::SensorFrame;
using ivanna::neuromorphic::NeuroFrame;
using ivanna::neuromorphic::NeuroSensorKind;
using ivanna::neuromorphic::monotonicNowUs;

constexpr float kSr = 48000.0f;
constexpr double kPi = 3.14159265358979323846;

// Señal determinista (LCG) + senos: misma entrada en todas las corridas.
std::vector<float> makeSignal(int n, float amp = 0.5f) {
    std::vector<float> s((std::size_t)n);
    std::uint32_t state = 0x12345678u;
    for (int i = 0; i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        const float noise =
            (static_cast<float>((state >> 16) & 0xffffu) / 32768.0f - 1.0f) * 0.05f;
        const float t = (float)i / kSr;
        s[(std::size_t)i] =
            amp * (0.6f * std::sin(2.0f * (float)kPi * 440.0f * t) +
                   0.3f * std::sin(2.0f * (float)kPi * 1230.0f * t) +
                   0.1f * std::sin(2.0f * (float)kPi * 5230.0f * t)) +
            noise;
    }
    return s;
}

std::vector<float> makeTone(float freqHz, float amp, int n) {
    std::vector<float> s((std::size_t)n);
    for (int i = 0; i < n; ++i)
        s[(std::size_t)i] =
            amp * std::sin(2.0f * (float)kPi * freqHz * (float)i / kSr);
    return s;
}

// Multi-tono con espectro inclinado (mucho grave, poco agudo).
std::vector<float> makeTiltedSignal(int n) {
    std::vector<float> s((std::size_t)n);
    const float freqs[5] = {100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f};
    const float amps[5] = {0.80f, 0.40f, 0.30f, 0.10f, 0.05f};
    for (int i = 0; i < n; ++i) {
        float v = 0.0f;
        for (int k = 0; k < 5; ++k)
            v += amps[k] * std::sin(2.0f * (float)kPi * freqs[k] * (float)i / kSr);
        s[(std::size_t)i] = 0.6f * v;
    }
    return s;
}

float peakOf(const std::vector<float>& s) {
    float p = 0.0f;
    for (float v : s) p = std::max(p, std::fabs(v));
    return p;
}

float rmsOf(const std::vector<float>& s, std::size_t from = 0) {
    double acc = 0.0;
    for (std::size_t i = from; i < s.size(); ++i) acc += (double)s[i] * (double)s[i];
    const std::size_t n = s.size() - from;
    return n == 0 ? 0.0f : (float)std::sqrt(acc / (double)n);
}

// Amplitud de un tono por Goertzel (ventana rectangular, sin normalizar
// por N/2: se devuelve la amplitud del seno).
float goertzelAmp(const std::vector<float>& s, std::size_t from, float freqHz) {
    const double w = 2.0 * kPi * (double)freqHz / (double)kSr;
    const double cw = std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < s.size(); ++i) {
        const double x = (double)s[i];
        const double s0 = x + 2.0 * cw * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double n = (double)(s.size() - from);
    const double real = s1 - s2 * cw;
    const double imag = s2 * std::sin(w);
    return (float)(2.0 * std::sqrt(real * real + imag * imag) / n);
}

float maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    float m = 0.0f;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

bool allFinite(const std::vector<float>& s) {
    for (float v : s)
        if (!std::isfinite(v)) return false;
    return true;
}

// Frame de sensor válido: error plano 0, coherencia 1, nivel -20 dBFS.
SensorFrame validFrame(std::uint64_t index = 1) {
    SensorFrame f{};
    f.valid = true;
    f.frame_index = index;
    f.level_dbfs = -20.0f;
    for (int b = 0; b < SensorFrame::kBands; ++b) {
        f.magnitude_db[b] = -20.0f;
        f.error_db[b] = 0.0f;
        f.coherence[b] = 1.0f;
    }
    return f;
}

// Procesa por bloques re-enviando el frame (frescura 500 ms = 24000 muestras).
void processWithFrame(HyperStateOperator& op, std::vector<float>& l,
                      std::vector<float>& r, const SensorFrame& base) {
    const int n = (int)l.size();
    std::uint64_t idx = base.frame_index;
    for (int off = 0; off < n; off += 8192) {
        SensorFrame f = base;
        f.frame_index = idx++;
        ASSERT_TRUE(op.setSensorFrame(f));
        const int chunk = std::min(8192, n - off);
        op.processBlock(l.data() + off, r.data() + off, chunk);
    }
}

int bandNearest(HyperStateOperator& op, float freqHz) {
    int best = 0;
    float bestD = 1e30f;
    for (int b = 0; b < HyperStateOperator::kBands; ++b) {
        const float d = std::fabs(std::log(op.bandCenterHz(b)) - std::log(freqHz));
        if (d < bestD) {
            bestD = d;
            best = b;
        }
    }
    return best;
}

}  // namespace

TEST(HyperStateOperator, AmountZeroIsBitExactIdentity) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(0.0f);
    auto in = makeSignal(48000);
    auto l = in, r = in;
    op.processBlock(l.data(), r.data(), (int)l.size());
    ASSERT_EQ(l.size(), in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        EXPECT_EQ(l[i], in[i]);
        EXPECT_EQ(r[i], in[i]);
    }
    EXPECT_EQ(op.status().mode, HyperMode::Off);
}

TEST(HyperStateOperator, ApproximateModeIsAudibleOnRouteA) {
    // Sin SensorFrame (el sensor de micrófono aún no alimenta frames),
    // con amount=1 el operador DEBE sonar: modo APROXIMADO.
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    auto in = makeTiltedSignal(48000);
    auto l = in, r = in;
    op.processBlock(l.data(), r.data(), (int)l.size());
    EXPECT_EQ(op.status().mode, HyperMode::Approximate);
    EXPECT_TRUE(allFinite(l));
    EXPECT_LE(peakOf(l), peakOf(in));
    // El balance espectral se corrige (solo corte, nunca refuerzo): el
    // grave (100 Hz, el más fuerte) baja; el agudo (10 kHz) queda
    // prácticamente intacto; la relación agudo/grave sube.
    const std::size_t tail = l.size() - 8192;
    const float inLow = goertzelAmp(in, tail, 100.0f);
    const float outLow = goertzelAmp(l, tail, 100.0f);
    const float inHigh = goertzelAmp(in, tail, 10000.0f);
    const float outHigh = goertzelAmp(l, tail, 10000.0f);
    EXPECT_LT(outLow, inLow * 0.95f);
    EXPECT_GT(outHigh, inHigh * 0.95f);
    EXPECT_LT(outHigh, inHigh * 1.05f);
    EXPECT_GT((outHigh / outLow), (inHigh / inLow) * 1.05f);
    EXPECT_GT(maxAbsDiff(l, in), 0.01f);
    EXPECT_GT(op.status().spikeRate, 0.0f);  // el NPE corre de verdad
}

TEST(HyperStateOperator, MeasuredModeCutsBandAboveTarget) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    const int band = bandNearest(op, 1000.0f);
    SensorFrame f = validFrame(1);
    f.error_db[band] = 6.0f;  // medido 6 dB por encima del objetivo
    auto in = makeTone(1000.0f, 0.5f, 48000);
    auto l = in, r = in;
    processWithFrame(op, l, r, f);
    EXPECT_EQ(op.status().mode, HyperMode::Measured);
    EXPECT_TRUE(op.status().sensorFresh);
    EXPECT_TRUE(allFinite(l));
    EXPECT_LE(peakOf(l), peakOf(in));
    const float inAmp = goertzelAmp(in, in.size() - 8192, 1000.0f);
    const float outAmp = goertzelAmp(l, l.size() - 8192, 1000.0f);
    EXPECT_LT(outAmp, inAmp * 0.75f);  // corte de ~6 dB
}

TEST(HyperStateOperator, MeasuredModeBoostsBandBelowTargetWithoutRaisingPeak) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    const int band = bandNearest(op, 1000.0f);
    SensorFrame f = validFrame(1);
    f.error_db[band] = -6.0f;  // medido 6 dB por debajo del objetivo
    // Dos tonos: el de 200 Hz fija el pico (0.5); el de 1 kHz (0.1) es
    // el que se refuerza, con margen hasta el pico.
    auto in = makeTone(200.0f, 0.5f, 48000);
    auto t1k = makeTone(1000.0f, 0.1f, 48000);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] += t1k[i];
    auto l = in, r = in;
    processWithFrame(op, l, r, f);
    EXPECT_TRUE(allFinite(l));
    EXPECT_LE(peakOf(l), peakOf(in) + 1e-6f);
    const float inAmp = goertzelAmp(in, in.size() - 8192, 1000.0f);
    const float outAmp = goertzelAmp(l, l.size() - 8192, 1000.0f);
    EXPECT_GT(outAmp, inAmp * 1.5f);  // refuerzo de ~6 dB
}

TEST(HyperStateOperator, CoherenceZeroMeansNoMeasuredCorrection) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    const int band = bandNearest(op, 1000.0f);
    SensorFrame f = validFrame(1);
    f.error_db[band] = 6.0f;
    for (int b = 0; b < SensorFrame::kBands; ++b) f.coherence[b] = 0.0f;
    auto in = makeTone(1000.0f, 0.5f, 48000);
    auto l = in, r = in;
    processWithFrame(op, l, r, f);
    // Sin confianza, no hay corrección medida: el tono queda intacto
    // (dentro de la pequeña modulación neuro, ≤4 %).
    const float inAmp = goertzelAmp(in, in.size() - 8192, 1000.0f);
    const float outAmp = goertzelAmp(l, l.size() - 8192, 1000.0f);
    EXPECT_GT(outAmp, inAmp * 0.96f);
    EXPECT_LT(outAmp, inAmp * 1.02f);
}

TEST(HyperStateOperator, InvalidFramesAreRejected) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    SensorFrame bad = validFrame(1);
    bad.valid = false;
    EXPECT_FALSE(op.setSensorFrame(bad));
    bad = validFrame(1);
    bad.error_db[3] = std::nanf("");
    EXPECT_FALSE(op.setSensorFrame(bad));
    bad = validFrame(1);
    bad.coherence[3] = 2.0f;
    EXPECT_FALSE(op.setSensorFrame(bad));
    bad = validFrame(1);
    bad.level_dbfs = 50.0f;
    EXPECT_FALSE(op.setSensorFrame(bad));
    EXPECT_EQ(op.status().framesReceived, 0u);
    EXPECT_TRUE(op.setSensorFrame(validFrame(1)));
    // El frame en staging se aplica en el hilo de audio: hay que
    // procesar muestras antes de que cuente como recibido/duplicado.
    auto in = makeSignal(256);
    auto l = in, r = in;
    op.processBlock(l.data(), r.data(), (int)l.size());
    EXPECT_EQ(op.status().framesReceived, 1u);
    EXPECT_FALSE(op.setSensorFrame(validFrame(1)));  // duplicado
}

TEST(HyperStateOperator, StaleSensorFallsBackToApproximate) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    SensorFrame f = validFrame(1);
    ASSERT_TRUE(op.setSensorFrame(f));
    auto in = makeSignal(8192);
    auto l = in, r = in;
    op.processBlock(l.data(), r.data(), (int)l.size());
    EXPECT_EQ(op.status().mode, HyperMode::Measured);
    // Sin más frames, tras > 500 ms el modo vuelve a APROXIMADO.
    auto more = makeSignal(48000);
    auto l2 = more, r2 = more;
    op.processBlock(l2.data(), r2.data(), (int)l2.size());
    EXPECT_EQ(op.status().mode, HyperMode::Approximate);
    EXPECT_FALSE(op.status().sensorFresh);
}

TEST(HyperStateOperator, NeuroFrameChangesOutput) {
    // Ablación neuro: mismo audio, con y sin frames EEG reales.
    auto run = [](bool withEeg) {
        HyperStateOperator op;
        op.prepare(kSr);
        op.setAmount(1.0f);
        if (withEeg) {
            // 8 frames EEG reales (kind EegInEar, respuesta baja → el
            // bucle pide ganancia). Los timestamps terminan en "ahora":
            // el controlador exige now >= lastFrame (un frame con
            // timestamp en el futuro nunca es fresco).
            const std::uint64_t now = monotonicNowUs();
            for (int k = 0; k < 8; ++k) {
                NeuroFrame nf{};
                nf.kind = NeuroSensorKind::EegInEar;
                nf.timestampUs = now - (std::uint64_t)((7 - k) * 10000);
                nf.quality = 0.9f;
                for (int b = 0; b < 8; ++b) {
                    nf.response[(std::size_t)b] = 0.2f;
                    nf.coherence[(std::size_t)b] = 0.8f;
                }
                EXPECT_TRUE(op.setNeuroFrame(nf));
            }
        }
        SensorFrame f = validFrame(1);
        auto in = makeSignal(16384);
        auto l = in, r = in;
        processWithFrame(op, l, r, f);
        return std::make_pair(l, op.status().neuroActive);
    };
    const auto base = run(false);
    const auto withEeg = run(true);
    EXPECT_FALSE(base.second);
    EXPECT_TRUE(withEeg.second);
    EXPECT_GT(maxAbsDiff(base.first, withEeg.first), 1e-3f);
}

TEST(HyperStateOperator, NeuroFrameValidation) {
    HyperStateOperator op;
    op.prepare(kSr);
    NeuroFrame bad{};
    bad.kind = NeuroSensorKind::Unknown;
    bad.quality = 0.9f;
    EXPECT_FALSE(op.setNeuroFrame(bad));
    NeuroFrame low{};
    low.kind = NeuroSensorKind::EegInEar;
    low.quality = 0.1f;  // calidad bajo el mínimo
    EXPECT_FALSE(op.setNeuroFrame(low));
    NeuroFrame good{};
    good.kind = NeuroSensorKind::EegInEar;
    good.timestampUs = monotonicNowUs();
    good.quality = 0.9f;
    for (int b = 0; b < 8; ++b) {
        good.response[(std::size_t)b] = 0.5f;
        good.coherence[(std::size_t)b] = 0.8f;
    }
    EXPECT_TRUE(op.setNeuroFrame(good));
}

TEST(HyperStateOperator, EmfAblation) {
    // Ablación EMF: misma señal, con y sin back-EMF real medida.
    auto run = [](bool withEmf) {
        HyperStateOperator op;
        op.prepare(kSr);
        op.setAmount(1.0f);
        SensorFrame f = validFrame(1);
        auto in = makeSignal(16384);
        auto l = in, r = in;
        std::uint64_t idx = 1;
        for (int off = 0; off < 16384; ++off) {
            if ((off & 8191) == 0) {
                SensorFrame fr = f;
                fr.frame_index = idx++;
                op.setSensorFrame(fr);
            }
            if (withEmf) {
                // Back-EMF real medida: 0.5 V @ 50 Hz (cono moviéndose).
                const float t = (float)off / kSr;
                op.setMeasuredEmf(0.5f * std::sin(2.0f * (float)kPi * 50.0f * t));
            }
            op.process(l[(std::size_t)off], r[(std::size_t)off]);
        }
        return std::make_pair(l, op.status().emfFresh);
    };
    const auto base = run(false);
    const auto withEmf = run(true);
    EXPECT_FALSE(base.second);
    EXPECT_TRUE(withEmf.second);
    EXPECT_GT(maxAbsDiff(base.first, withEmf.first), 1e-3f);
}

TEST(HyperStateOperator, MeasuredViObserverIsSane) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    SensorFrame f = validFrame(1);
    ASSERT_TRUE(op.setSensorFrame(f));
    // V=1.0 V, I=0.1 A constantes: el observador estima v=(1−0.6)/5.
    auto in = makeSignal(4096);
    auto l = in, r = in;
    for (int i = 0; i < 4096; ++i) {
        op.setMeasuredVI(1.0f, 0.1f);
        op.process(l[(std::size_t)i], r[(std::size_t)i]);
    }
    const auto st = op.status();
    EXPECT_TRUE(st.emfFresh);
    EXPECT_GT(st.emfVelocityMps, 0.0f);
    EXPECT_GT(st.emfDisplacementMm, 1.0f);  // el cono se desplaza (mm)
}

TEST(HyperStateOperator, Deterministic) {
    auto run = []() {
        kalmanInit();  // reset del Kalman global compartido (ver arriba)
        HyperStateOperator op;
        op.prepare(kSr);
        op.setAmount(0.7f);
        SensorFrame f = validFrame(1);
        auto in = makeSignal(32768);
        auto l = in, r = in;
        processWithFrame(op, l, r, f);
        return l;
    };
    const auto a = run();
    const auto b = run();
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i], b[i]);
}

TEST(HyperStateOperator, FiniteAndPeakGuard) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    SensorFrame f = validFrame(1);
    for (int b = 0; b < SensorFrame::kBands; ++b)
        f.error_db[b] = (b % 2 == 0) ? 6.0f : -6.0f;
    auto in = makeSignal(48000, 0.9f);
    in[1000] = 1.5f;   // transitorio fuerte
    in[2000] = -1.2f;
    in[3000] = std::nanf("");  // NaN en la entrada: no debe propagarse
    auto l = in, r = in;
    processWithFrame(op, l, r, f);
    EXPECT_TRUE(allFinite(l));
    EXPECT_TRUE(allFinite(r));
    // El pico de salida nunca supera el pico de la entrada (finita).
    float inPeak = 0.0f;
    for (float v : in)
        if (std::isfinite(v)) inPeak = std::max(inPeak, std::fabs(v));
    EXPECT_LE(peakOf(l), inPeak + 1e-6f);
    EXPECT_LE(peakOf(r), inPeak + 1e-6f);
}

TEST(HyperStateOperator, VolterraGate) {
    HyperStateOperator op;
    op.prepare(kSr);
    EXPECT_TRUE(op.isVolterraBypass());
    float h1[64] = {};
    float h2d[64] = {};
    h1[0] = 1.0f;
    h2d[0] = 0.05f;
    h1[1] = 0.1f;
    EXPECT_TRUE(op.setVolterraCoefficients(h1, h2d, 64));
    float bad[64] = {};
    bad[0] = std::nanf("");
    EXPECT_FALSE(op.setVolterraCoefficients(bad, h2d, 64));
    bad[0] = 9.0f;  // |c| > 8
    EXPECT_FALSE(op.setVolterraCoefficients(bad, h2d, 64));
    EXPECT_FALSE(op.setVolterraCoefficients(h1, h2d, 63));  // longitud
    // La carga se aplica en el hilo de audio (1× por bloque).
    auto in = makeSignal(4096);
    auto l = in, r = in;
    op.setAmount(1.0f);
    SensorFrame f = validFrame(1);
    processWithFrame(op, l, r, f);
    EXPECT_FALSE(op.isVolterraBypass());
}

TEST(HyperStateOperator, TargetDbShiftsApproximateCut) {
    // Curva objetivo élite: con target +3 dB en la banda de 100 Hz, el
    // corte aproximado sobre el grave es MENOR que con target plano
    // (la corrección doma hacia la curva, no hacia plano). Sigue siendo
    // solo corte: nunca refuerza por encima de la entrada.
    auto run = [](bool withTarget) {
        HyperStateOperator op;
        op.prepare(kSr);
        op.setAmount(1.0f);
        // El default ya NO es plano (es Harman 2018): la corrida "plana"
        // fija el target a 0 explícitamente para aislar el efecto.
        float flat24[24] = {};
        op.setTargetDb(flat24);
        if (withTarget) {
            // +12 dB (máximo) en la banda ~82 Hz: el corte del grave en
            // esta señal está SATURADO a −3 dB (la banda está ~11 dB por
            // encima de la referencia), así que un shift de +3 dB no lo
            // movería; +12 dB lo desatura (corrección → 0, sin corte).
            float tgt[24] = {};
            tgt[0] = 12.0f;
            op.setTargetDb(tgt);
        }
        auto in = makeTiltedSignal(48000);
        auto l = in, r = in;
        op.processBlock(l.data(), r.data(), (int)l.size());
        return std::make_pair(l, in);
    };
    const auto flat = run(false);
    const auto elite = run(true);
    const std::size_t tail = flat.first.size() - 8192;
    const float inLow = goertzelAmp(flat.second, tail, 100.0f);
    const float outLowFlat = goertzelAmp(flat.first, tail, 100.0f);
    const float outLowElite = goertzelAmp(elite.first, tail, 100.0f);
    EXPECT_LT(outLowFlat, inLow * 0.95f);       // plano: corta el grave
    EXPECT_GT(outLowElite, outLowFlat);         // élite: corta menos
    EXPECT_LE(outLowElite, inLow);              // sigue siendo solo corte
}

TEST(HyperStateOperator, TargetDbValidation) {
    HyperStateOperator op;
    op.prepare(kSr);
    float tgt[24] = {};
    tgt[3] = std::nanf("");
    tgt[5] = 99.0f;    // se recorta a +12
    tgt[7] = -99.0f;   // se recorta a −12
    op.setTargetDb(tgt);
    auto in = makeSignal(256);
    auto l = in, r = in;
    op.setAmount(1.0f);
    op.processBlock(l.data(), r.data(), (int)l.size());  // aplica el staging
    // NaN = se conserva el valor vigente (Harman por defecto, no 0).
    EXPECT_FLOAT_EQ(op.targetDb(3),
                    eliteTargetDbAt(EliteTargetKind::Harman2018,
                                    op.bandCenterHz(3)));
    EXPECT_FLOAT_EQ(op.targetDb(5), 12.0f);
    EXPECT_FLOAT_EQ(op.targetDb(7), -12.0f);
    EXPECT_FLOAT_EQ(op.targetDb(99), 0.0f);  // banda fuera de rango
    op.setTargetDb(nullptr);                 // no debe crashear
}

TEST(HyperStateOperator, DefaultTargetIsHarman2018) {
    // El target por defecto del operador es Harman AE/OE 2018 (no plano):
    // cada banda vale la interpolación de la curva en su centro.
    HyperStateOperator op;
    op.prepare(kSr);
    for (int b = 0; b < 24; ++b) {
        EXPECT_NEAR(op.targetDb(b),
                    eliteTargetDbAt(EliteTargetKind::Harman2018,
                                    op.bandCenterHz(b)),
                    0.01f);
    }
    EXPECT_EQ(op.targetKind(), EliteTargetKind::Harman2018);
    EXPECT_FLOAT_EQ(op.bassTrimDb(), 0.0f);
    // Los centros ERB del operador coinciden con los del header de curvas
    // (si la fórmula cambia en un lado, este test lo detecta).
    float centers[24];
    eliteErbCenters24(kSr, centers);
    for (int b = 0; b < 24; ++b) {
        EXPECT_NEAR(op.bandCenterHz(b), centers[b], centers[b] * 0.001f);
    }
}

TEST(HyperStateOperator, EliteTargetKindAndBassTrim) {
    HyperStateOperator op;
    op.prepare(kSr);
    auto apply = [&]() {
        auto in = makeSignal(256);
        auto l = in, r = in;
        op.setAmount(1.0f);
        op.processBlock(l.data(), r.data(), (int)l.size());
    };
    const int lowBand = bandNearest(op, 50.0f);
    const int midBand = bandNearest(op, 1000.0f);
    // Élite-lean: graves −3 dB respecto a Harman, resto igual.
    op.setEliteTarget(EliteTargetKind::HarmanEliteLean, 0.0f);
    apply();
    EXPECT_NEAR(op.targetDb(lowBand),
                eliteTargetDbAt(EliteTargetKind::HarmanEliteLean,
                                op.bandCenterHz(lowBand)),
                0.01f);
    EXPECT_NEAR(op.targetDb(midBand),
                eliteTargetDbAt(EliteTargetKind::HarmanEliteLean,
                                op.bandCenterHz(midBand)),
                0.01f);
    EXPECT_EQ(op.targetKind(), EliteTargetKind::HarmanEliteLean);
    // Bass trim +3 dB: sube el grave, no toca 1 kHz. La banda grave
    // (centro ~82 Hz) está en la zona de taper (80–250 Hz): recibe
    // 3 dB × w, con w = (ln250 − ln f)/(ln250 − ln80). La banda de
    // 1 kHz vale el Harman en SU centro (~975 Hz, ≈ −0,03 dB), no 0.
    const float wLow =
        (std::log(250.0f) - std::log(op.bandCenterHz(lowBand))) /
        (std::log(250.0f) - std::log(80.0f));
    op.setEliteTarget(EliteTargetKind::Harman2018, 3.0f);
    apply();
    EXPECT_NEAR(op.targetDb(lowBand),
                eliteTargetDbAt(EliteTargetKind::Harman2018,
                                op.bandCenterHz(lowBand)) +
                    3.0f * wLow,
                0.01f);
    EXPECT_NEAR(op.targetDb(midBand),
                eliteTargetDbAt(EliteTargetKind::Harman2018,
                                op.bandCenterHz(midBand)),
                0.01f);
    EXPECT_FLOAT_EQ(op.bassTrimDb(), 3.0f);
    // Bass trim −3 dB y recorte del trim fuera de rango.
    op.setBassTrimDb(-3.0f);
    apply();
    EXPECT_FLOAT_EQ(op.bassTrimDb(), -3.0f);
    op.setBassTrimDb(99.0f);
    apply();
    EXPECT_FLOAT_EQ(op.bassTrimDb(), 3.0f);
    // Kind inválido → Harman 2018 (fail-closed).
    op.setEliteTarget(static_cast<EliteTargetKind>(99), 0.0f);
    apply();
    EXPECT_EQ(op.targetKind(), EliteTargetKind::Harman2018);
}

TEST(HyperStateOperator, SensorContract) {
    EXPECT_EQ(SensorFrame::kBands, 24);
    EXPECT_EQ(HyperStateOperator::kBands, 24);
}

TEST(HyperStateOperator, Throughput) {
    HyperStateOperator op;
    op.prepare(kSr);
    op.setAmount(1.0f);
    SensorFrame f = validFrame(1);
    auto in = makeSignal(48000 * 5);
    auto l = in, r = in;
    // Calentamiento (estados, cachés).
    processWithFrame(op, l, r, f);
    const auto t0 = std::chrono::steady_clock::now();
    processWithFrame(op, l, r, f);
    const auto t1 = std::chrono::steady_clock::now();
    const double sec = std::chrono::duration<double>(t1 - t0).count();
    const double rt = 5.0 / sec;
    std::printf("[hyper] throughput (SensorFrame, K=64 identidad): %.1fx RT\n", rt);
    EXPECT_GT(rt, 1.0);
    // Con kernel Volterra medido cargado (K=64, no identidad).
    float h1[64] = {};
    float h2d[64] = {};
    h1[0] = 1.0f;
    h2d[0] = 0.05f;
    h1[1] = 0.1f;
    ASSERT_TRUE(op.setVolterraCoefficients(h1, h2d, 64));
    const auto t2 = std::chrono::steady_clock::now();
    processWithFrame(op, l, r, f);
    const auto t3 = std::chrono::steady_clock::now();
    const double sec2 = std::chrono::duration<double>(t3 - t2).count();
    const double rt2 = 5.0 / sec2;
    std::printf("[hyper] throughput (Volterra K=64 no identidad): %.1fx RT\n", rt2);
    EXPECT_GT(rt2, 1.0);
    (void)rmsOf(l);
}
