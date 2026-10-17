// v2.5.41 — PunchEngine: impacto predictivo por bandas. Cada test fija una propiedad medible del motor con señales
// sintéticas (bombo, tonos, ruido). Es medición en host, NO escucha en dispositivo.
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <vector>
#include "include/punch_engine.hpp"

using namespace ivanna;
static constexpr float kSr = 48000.0f;
static double db(double x) { return 20.0 * std::log10(std::max(x, 1e-12)); }

struct Sig { std::vector<float> l, r; };
static Sig zeros(int n) { return {std::vector<float>(n, 0.f), std::vector<float>(n, 0.f)}; }

// Bombo: ataque de 1 ms hacia 60 Hz, decaimiento de ~80 ms.
static void addKick(Sig& s, int at, float amp, float panL = 1.f, float panR = 1.f) {
    const int len = static_cast<int>(0.4f * kSr);
    for (int i = 0; i < len && at + i < static_cast<int>(s.l.size()); ++i) {
        const float t = i / kSr;
        const float env = std::min(1.f, t / 0.001f) * std::exp(-t / 0.08f);
        const float v = amp * env * static_cast<float>(std::sin(2.0 * M_PI * 60.0 * t));
        s.l[at + i] += panL * v; s.r[at + i] += panR * v;
    }
}
static void addTone(Sig& s, float f, float amp) {
    for (size_t i = 0; i < s.l.size(); ++i) { const float v = amp * static_cast<float>(std::sin(2.0 * M_PI * f * i / kSr)); s.l[i] += v; s.r[i] += v; }
}
static void run(PunchEngine& p, Sig& s, int block) {
    for (size_t o = 0; o < s.l.size(); o += block) {
        const int n = static_cast<int>(std::min<size_t>(block, s.l.size() - o));
        p.process(&s.l[o], &s.r[o], n);
    }
}
static double rms(const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += double(x[i]) * x[i]; return std::sqrt(s / (b - a)); }
static float peak(const std::vector<float>& x, int a, int b) { float m = 0; for (int i = a; i < b; ++i) m = std::fmax(m, std::fabs(x[i])); return m; }

TEST(Punch, AmountZeroIsBitExactBypassWithZeroLatency) {
    PunchEngine p; p.prepare(kSr); p.setAmount(0.f);
    Sig a = zeros(9600); uint32_t s = 1; for (auto& v : a.l) { s = s * 1664525u + 1013904223u; v = ((s >> 8) & 0xFFFF) / 32768.f - 1.f; }
    a.r = a.l; Sig ref = a;
    run(p, a, 333);
    for (size_t i = 0; i < a.l.size(); ++i) { ASSERT_EQ(a.l[i], ref.l[i]); ASSERT_EQ(a.r[i], ref.r[i]); }
    EXPECT_EQ(p.latencySamples(), 0);
}

TEST(Punch, SteadyTonesAcrossTheCrossoversStayFlatApartFromTheDocumentedSustainTrim) {
    // Sin ataques el motor solo aplica el recorte de sostén (-1.4 dB a amount = 1): la suma LR4 compensada en
    // fase debe ser PLANA, incluidos los cruces en 160 Hz y 2.5 kHz.
    for (float f : {40.f, 100.f, 160.f, 400.f, 1000.f, 2500.f, 5000.f, 12000.f}) {
        PunchEngine p; p.prepare(kSr); p.setAmount(1.f);
        Sig s = zeros(static_cast<int>(2.5f * kSr)); addTone(s, f, 0.1f);
        run(p, s, 480);
        const int a = static_cast<int>(1.5f * kSr), b = static_cast<int>(2.5f * kSr);
        const double g = db(rms(s.l, a, b) / (0.1 / std::sqrt(2.0)));
        EXPECT_GT(g, -1.75) << f << " Hz"; EXPECT_LT(g, -1.05) << f << " Hz";
    }
}

TEST(Punch, KickAttackIsBoostedAndSustainIsTrimmed) {
    Sig in = zeros(static_cast<int>(2.0f * kSr)); addTone(in, 80.f, 0.05f);
    const int at = static_cast<int>(1.0f * kSr); addKick(in, at, 0.3f);
    Sig out = in; PunchEngine p; p.prepare(kSr); p.setAmount(1.f); run(p, out, 480);
    const int D = p.latencySamples();
    ASSERT_GT(D, 0);
    // Pico del ataque (ventana tras el golpe retardado) frente al pico de la entrada retardada: realce de graves.
    const float pin = peak(in.l, at, at + 1440), pout = peak(out.l, at + D, at + D + 1440);
    EXPECT_GT(db(pout / pin), 2.0) << "el ataque debe subir al menos 2 dB";
    EXPECT_LT(db(pout / pin), 4.6) << "la guarda de excursión (~-6 dBFS, ±1 dB por el cruce) limita el realce de graves";
    // Sostén (tono estable ANTES del golpe): recortado ~1.4 dB, no más.
    const double sus = db(rms(out.l, 40000 - 20000, 40000) / rms(in.l, 40000 - 20000 - D, 40000 - D));
    EXPECT_GT(sus, -1.8); EXPECT_LT(sus, -1.0);
}

TEST(Punch, LookaheadMakesTheBoostArriveWithTheOnsetNotAfterIt) {
    // Banda MEDIA (1 kHz, retardo de grupo del cruce < 0.3 ms): una ráfaga que empieza de golpe sobre un tono suave.
    // Con anticipación de 1.5 ms la ganancia ya está arriba cuando suena el primer ciclo.
    const int N = static_cast<int>(1.6f * kSr), at = static_cast<int>(1.0f * kSr);
    Sig in = zeros(N); addTone(in, 1000.f, 0.02f);
    for (int i = at; i < N; ++i) {
        const float t = (i - at) / kSr, env = std::min(1.f, t / 0.0002f) * std::exp(-t / 0.05f);
        const float v = 0.2f * env * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t));
        in.l[i] += v; in.r[i] += v;
    }
    Sig out = in; PunchEngine p; p.prepare(kSr); p.setAmount(1.f); run(p, out, 64);
    const int D = p.latencySamples(), c = static_cast<int>(0.001f * kSr);     // un ciclo de 1 kHz
    // Pico del PRIMER ciclo tras el inicio (ya retardado D) frente al mismo tramo de la entrada.
    const float pin = peak(in.l, at, at + c), pout = peak(out.l, at + D, at + D + c + 24);
    EXPECT_GT(db(pout / pin), 1.2) << "sin anticipación el primer ciclo saldría sin realce";
}

TEST(Punch, CrestFactorRisesWithoutRaisingThePeak) {
    Sig in = zeros(static_cast<int>(4.0f * kSr)); addTone(in, 200.f, 0.04f); addTone(in, 3000.f, 0.02f);
    for (int k = 0; k < 6; ++k) addKick(in, static_cast<int>((0.4f + 0.6f * k) * kSr), 0.3f);
    Sig out = in; PunchEngine p; p.prepare(kSr); p.setAmount(1.f); run(p, out, 480);
    const int a = static_cast<int>(0.5f * kSr), b = static_cast<int>(3.8f * kSr);
    const double crestIn = db(peak(in.l, a, b) / rms(in.l, a, b));
    const double crestOut = db(peak(out.l, a, b) / rms(out.l, a, b));
    EXPECT_GT(crestOut - crestIn, 0.8) << "debe subir la relación ataque/sostén";
    EXPECT_LE(peak(out.l, a, b), 0.56f) << "la guarda de -6 dBFS de la banda grave limita el pico (+ realce de medios/agudos)";
}

TEST(Punch, BassExcursionGuardNeverLetsTheLowBandExceedMinus6dBFS) {
    Sig s = zeros(static_cast<int>(1.5f * kSr)); addKick(s, 12000, 0.48f);       // ya a -6.4 dBFS
    PunchEngine p; p.prepare(kSr); p.setAmount(1.f);
    bool guardSeen = false; float tel[PunchEngine::kTelemetrySize];
    for (size_t o = 0; o < s.l.size(); o += 480) {
        p.process(&s.l[o], &s.r[o], 480); p.telemetry(tel); guardSeen = guardSeen || tel[4] > 0.5f;
    }
    EXPECT_LE(peak(s.l, 0, static_cast<int>(s.l.size())), 0.5f * 1.12f) << "tope de graves ~-6 dBFS con ±1 dB de tolerancia por la fuga del cruce";
    EXPECT_LT(peak(s.l, 0, static_cast<int>(s.l.size())), 0.48f * 1.2f) << "y muy por debajo de lo que daría el realce sin guarda (0.48 x 1.9)";
    EXPECT_TRUE(guardSeen) << "la telemetría debe reportar que la guarda actuó";
}

TEST(Punch, HeadroomGuardNeverRaisesAMasterAlreadyAtTheCeiling) {
    Sig in = zeros(static_cast<int>(2.0f * kSr));
    for (int k = 0; k < 4; ++k) addKick(in, static_cast<int>((0.3f + 0.4f * k) * kSr), 0.95f);
    Sig out = in; PunchEngine p; p.prepare(kSr); p.setAmount(1.f); run(p, out, 480);
    EXPECT_LE(peak(out.l, 0, static_cast<int>(out.l.size())), peak(in.l, 0, static_cast<int>(in.l.size())) * 1.001f)
        << "con el pico en el techo no se inventa headroom: el limitador posterior no debe bombear";
}

TEST(Punch, StereoImageIsPreservedBecauseTheGainIsLinked) {
    Sig in = zeros(static_cast<int>(2.0f * kSr));
    for (int k = 0; k < 4; ++k) addKick(in, static_cast<int>((0.3f + 0.4f * k) * kSr), 0.3f, 0.7f, 0.3f);
    for (size_t i = 0; i < in.l.size(); ++i) {                                   // TODO el contenido con el mismo paneo 0.7/0.3
        const float v = 0.01f * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / kSr));
        in.l[i] += 0.7f * v; in.r[i] += 0.3f * v;
    }
    Sig out = in; PunchEngine p; p.prepare(kSr); p.setAmount(1.f); run(p, out, 480);
    const int a = static_cast<int>(0.2f * kSr), b = static_cast<int>(1.9f * kSr), D = p.latencySamples();
    const double balIn  = db(rms(in.l, a, b) / rms(in.r, a, b));
    const double balOut = db(rms(out.l, a + D, b + D) / rms(out.r, a + D, b + D));
    EXPECT_NEAR(balOut, balIn, 0.15);
}

TEST(Punch, OutputIsIndependentOfBlockSize) {
    Sig base = zeros(static_cast<int>(1.5f * kSr)); addTone(base, 90.f, 0.05f); addTone(base, 1800.f, 0.02f);
    addKick(base, 24000, 0.3f); addKick(base, 50000, 0.25f);
    Sig ref = base; { PunchEngine p; p.prepare(kSr); p.setAmount(0.7f); run(p, ref, 1); }
    for (int block : {64, 333, 4800}) {
        Sig o = base; PunchEngine p; p.prepare(kSr); p.setAmount(0.7f); run(p, o, block);
        for (size_t i = 0; i < o.l.size(); ++i) ASSERT_NEAR(o.l[i], ref.l[i], 1e-6f) << "bloque " << block << " muestra " << i;
    }
}

TEST(Punch, NonFiniteInputNeverPoisonsTheState) {
    PunchEngine p; p.prepare(kSr); p.setAmount(1.f);
    Sig s = zeros(4800); addTone(s, 100.f, 0.2f);
    s.l[100] = std::numeric_limits<float>::quiet_NaN(); s.r[200] = std::numeric_limits<float>::infinity();
    run(p, s, 480);
    for (size_t i = 0; i < s.l.size(); ++i) { ASSERT_TRUE(std::isfinite(s.l[i])); ASSERT_TRUE(std::isfinite(s.r[i])); }
    Sig t = zeros(4800); addTone(t, 100.f, 0.2f); run(p, t, 480);
    for (float v : t.l) ASSERT_TRUE(std::isfinite(v));
}

TEST(Punch, WorksAtOtherSampleRatesWithTheSameLookaheadInMilliseconds) {
    for (float sr : {44100.f, 48000.f, 96000.f}) {
        PunchEngine p; p.prepare(sr); p.setAmount(1.f);
        EXPECT_NEAR(p.lookaheadMs(), 1.5f, 0.05f) << sr;
        std::vector<float> l(4800, 0.1f), r(4800, 0.1f);
        p.process(l.data(), r.data(), 4800);
        for (float v : l) ASSERT_TRUE(std::isfinite(v));
    }
}

TEST(Punch, SwitchingOnAndOffNeverClicksAndReturnsToExactBypass) {
    PunchEngine p; p.prepare(kSr);
    Sig s = zeros(static_cast<int>(1.6f * kSr)); addTone(s, 440.f, 0.3f);
    Sig ref = s; float maxStepIn = 0; for (size_t i = 1; i < ref.l.size(); ++i) maxStepIn = std::fmax(maxStepIn, std::fabs(ref.l[i] - ref.l[i - 1]));
    p.setAmount(0.f); p.process(&s.l[0], &s.r[0], 9600);
    p.setAmount(1.f); p.process(&s.l[9600], &s.r[9600], 19200);
    p.setAmount(0.f); p.process(&s.l[28800], &s.r[28800], static_cast<int>(s.l.size()) - 28800);
    float maxStepOut = 0; for (size_t i = 1; i < s.l.size(); ++i) maxStepOut = std::fmax(maxStepOut, std::fabs(s.l[i] - s.l[i - 1]));
    EXPECT_LT(maxStepOut, maxStepIn * 1.6f) << "una discontinuidad de entrada/salida sería un clic";
    // Tras apagar y asentar (< 1.6 s) la salida vuelve a ser idéntica a la entrada: bypass exacto y latencia 0.
    const size_t tail = s.l.size() - 4800;
    for (size_t i = tail; i < s.l.size(); ++i) ASSERT_EQ(s.l[i], ref.l[i]);
    EXPECT_EQ(p.latencySamples(), 0);
}

// ── Sesgo de IMPACTO (drop del arco narrativo 4D) ──────────────────────────────────────────────────────────────
static Sig burst1k(float amp) {                       // ráfaga de 1 kHz sobre un tono suave: banda MEDIA (guarda de graves no interviene)
    const int N = static_cast<int>(1.6f * kSr), at = static_cast<int>(1.0f * kSr);
    Sig in = zeros(N); addTone(in, 1000.f, 0.02f);
    for (int i = at; i < N; ++i) {
        const float t = (i - at) / kSr, env = std::min(1.f, t / 0.0002f) * std::exp(-t / 0.05f);
        const float v = amp * env * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t));
        in.l[i] += v; in.r[i] += v;
    }
    return in;
}
static double burstGainDb(float bias, float amp, float amount) {
    const Sig in = burst1k(amp); Sig out = in;
    PunchEngine p; p.prepare(kSr); p.setAmount(amount); p.setImpactBias(bias); run(p, out, 64);
    const int at = static_cast<int>(1.0f * kSr), D = p.latencySamples(), w = static_cast<int>(0.004f * kSr);
    return db(peak(out.l, at + D, at + D + w + 24) / peak(in.l, at, at + w));
}

TEST(Punch, ImpactBiasRaisesTheAttackBoostWhenThereIsHeadroom) {
    const double plain = burstGainDb(0.0f, 0.2f, 1.f), drop = burstGainDb(1.0f, 0.2f, 1.f);
    EXPECT_GT(drop - plain, 0.6) << "el drop debe realzar el golpe de verdad (x1.45 de realce ~ +1 dB en medios)";
    EXPECT_LT(drop - plain, 2.0) << "pero con tope: +45 % de realce, no un salto de nivel";
}

TEST(Punch, ImpactBiasNeverPassesTheGuards) {
    // Master ya al techo: con o sin sesgo el pico de salida no puede superar al de entrada (guarda de headroom).
    Sig in = zeros(static_cast<int>(2.0f * kSr));
    for (int k = 0; k < 4; ++k) addKick(in, static_cast<int>((0.3f + 0.4f * k) * kSr), 0.95f);
    Sig out = in; PunchEngine p; p.prepare(kSr); p.setAmount(1.f); p.setImpactBias(1.f); run(p, out, 480);
    EXPECT_LE(peak(out.l, 0, static_cast<int>(out.l.size())), peak(in.l, 0, static_cast<int>(in.l.size())) * 1.001f);
    // Y la guarda de graves (~-6 dBFS) tampoco cede.
    Sig k = zeros(static_cast<int>(1.5f * kSr)); addKick(k, 12000, 0.48f);
    PunchEngine q; q.prepare(kSr); q.setAmount(1.f); q.setImpactBias(1.f); run(q, k, 480);
    EXPECT_LE(peak(k.l, 0, static_cast<int>(k.l.size())), 0.5f * 1.12f);
}

TEST(Punch, ZeroImpactBiasIsIdenticalToNeverSettingIt) {
    const Sig base = burst1k(0.2f);
    Sig a = base, b = base;
    PunchEngine p1; p1.prepare(kSr); p1.setAmount(0.8f); run(p1, a, 480);
    PunchEngine p2; p2.prepare(kSr); p2.setAmount(0.8f); p2.setImpactBias(0.0f); run(p2, b, 480);
    for (size_t i = 0; i < a.l.size(); ++i) ASSERT_EQ(a.l[i], b.l[i]);
}

TEST(Punch, NonFiniteImpactBiasIsIgnoredAndOutOfRangeIsClamped) {
    PunchEngine p; p.prepare(kSr);
    p.setImpactBias(0.5f); p.setImpactBias(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(p.impactBias(), 0.5f);
    p.setImpactBias(9.f);  EXPECT_FLOAT_EQ(p.impactBias(), 1.f);
    p.setImpactBias(-2.f); EXPECT_FLOAT_EQ(p.impactBias(), 0.f);
}
