// test_orbe_binaural.cpp — propiedades demostrables de ORBE (Binaural
// Esférico de Campo Cercano). El núcleo STFT es un banco de filtros de
// reconstrucción perfecta cuando no se modifica nada: mono intacto,
// bypass bit-exacto con amount=0, latencia = fftSize muestras.
#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include <vector>
#include "../spatial/OrbeBinaural.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;

double rms(const std::vector<float>& v, size_t from, size_t to) {
    double a = 0;
    for (size_t i = from; i < to && i < v.size(); ++i) a += (double)v[i] * v[i];
    return std::sqrt(a / (double)(to - from));
}
std::vector<float> tone(double f, double amp, double fs, size_t n, double phase = 0.0) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (float)(amp * std::sin(2 * kPi * f * (double)i / fs + phase));
    return v;
}
// Procesa en bloques de 128 (como IvannaFusionEngine::processStereo).
void runBlocks(ivanna::OrbeBinaural& o, std::vector<float>& l, std::vector<float>& r) {
    for (size_t s = 0; s < l.size(); s += 128) {
        const int n = (int)std::min<size_t>(128, l.size() - s);
        o.process(l.data() + s, r.data() + s, n);
    }
}
double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from) {
    double m = 0;
    for (size_t i = from; i < a.size(); ++i) m = std::max(m, std::fabs((double)a[i] - b[i]));
    return m;
}
}  // namespace

TEST(OrbeBinaural, AmountZeroIsBitExactBypass) {
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    o.setAmount(0.0f);
    std::mt19937 g(1);
    std::uniform_real_distribution<float> u(-1, 1);
    std::vector<float> l(48000), r(48000);
    for (auto& v : l) v = u(g);
    for (auto& v : r) v = u(g);
    auto l0 = l, r0 = r;
    runBlocks(o, l, r);
    EXPECT_EQ(l, l0);
    EXPECT_EQ(r, r0);
    EXPECT_FLOAT_EQ(o.appliedAmount(), 0.0f);
}

TEST(OrbeBinaural, MonoIsPreservedWithinFftRoundoff) {
    // L == R: ILD = 0, ITD = 0, side = 0 → la salida es la entrada
    // retardada fftSize muestras (reconstrucción perfecta del STFT).
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    o.setParams(p);
    std::mt19937 g(2);
    std::uniform_real_distribution<float> u(-0.9f, 0.9f);
    std::vector<float> l(96000);
    for (auto& v : l) v = u(g);
    std::vector<float> r = l, l0 = l;
    runBlocks(o, l, r);
    const int lat = o.latencySamples();
    EXPECT_EQ(lat, 512);
    // Después del priming, L y R deben ser iguales entre sí...
    EXPECT_LT(maxAbsDiff(l, r, 4096), 5e-4);
    // ...y cada uno igual a la entrada retardada `lat` muestras.
    double maxd = 0;
    for (size_t i = 4096; i < l.size(); ++i)
        maxd = std::max(maxd, std::fabs((double)l[i] - l0[i - lat]));
    EXPECT_LT(maxd, 5e-4) << "mono intacto salvo redondeo FFT";
}

TEST(OrbeBinaural, ImpulseLatencyIsExactlyFftSize) {
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    o.setParams(p);
    const int n = 48000;
    std::vector<float> l(n, 0.0f), r(n, 0.0f);
    // Impulso DESPUÉS del crossfade de activación (512 de latencia + 720
    // de rampa wet/blend): un impulso dentro de esa ventana se filtra en
    // vivo como (1-wet)·(1-blend) en su posición original — no es un
    // defecto de reconstrucción, es el fundido de arranque.
    l[5000] = 1.0f;
    r[5000] = 1.0f;  // impulso mono (centro)
    runBlocks(o, l, r);
    size_t peak = 0;
    float peakVal = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        if (std::fabs(l[i]) > peakVal) { peakVal = std::fabs(l[i]); peak = i; }
    }
    EXPECT_EQ((int)peak, 5000 + o.latencySamples()) << "latencia = fftSize muestras";
    EXPECT_NEAR(peakVal, 1.0f, 0.05f);
    // Sin cola espuria: energía fuera de ±8 muestras del pico < 1 %.
    double inE = 0, outE = 0;
    for (size_t i = 0; i < l.size(); ++i) {
        if (i < peak - 8 || i > peak + 8) outE += (double)l[i] * l[i];
        else inE += (double)l[i] * l[i];
    }
    EXPECT_LT(outE, 0.01 * inE);
}

TEST(OrbeBinaural, EstimatedAzimuthFollowsMeasuredItd) {
    // Estéreo con ITD conocido: R retrasado 0.3 ms respecto a L, mismo
    // nivel → la fuente está a la IZQUIERDA (L escucha primero) y la
    // banda de 500 Hz (índice 5, centro ≈ 498 Hz) debe estimar azimut
    // negativo, ≈ -0.6 rad para ITD 0.3 ms a r = 0.35 m.
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    o.setParams(p);
    const double fs = 48000.0;
    const int n = 48000 * 2;
    auto l = tone(500.0, 0.5, fs, n);
    std::vector<float> r(n, 0.0f);
    const int delaySamples = (int)(0.0003 * fs);  // 0.3 ms
    for (int i = delaySamples; i < n; ++i) r[i] = l[i - delaySamples];
    runBlocks(o, l, r);
    // Bandas alrededor de 500 Hz (índices 4-6 de 24): azimut < 0.
    double azSum = 0;
    int cnt = 0;
    for (int b = 4; b <= 6; ++b) { azSum += o.bandAzimuth(b); ++cnt; }
    EXPECT_LT(azSum / cnt, -0.2) << "azimut estimado a la izquierda (rad)";
    EXPECT_GT(azSum / cnt, -1.0);
}

TEST(OrbeBinaural, CloseRightSourceGetsStrongerRightEar) {
    // Fuente a la derecha y cerca: con amount=1 y distancia 0.35 m, la
    // salida derecha debe ser claramente más intensa que la izquierda.
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    p.distanceM = 0.35f;
    o.setParams(p);
    // R al 180 % de L (ILD +5.1 dB a la derecha); la salida debe
    // preservar/amplificar esa diferencia.
    const double fs = 48000.0;
    const int n = 48000 * 2;
    auto l = tone(1000.0, 0.3, fs, n);
    auto r = tone(1000.0, 0.54, fs, n);  // ILD ≈ +5.1 dB a la derecha
    runBlocks(o, l, r);
    const double rL = rms(l, 48000, 96000), rR = rms(r, 48000, 96000);
    EXPECT_GT(rR, rL * 1.2) << "ILD preservado/amplificado por ORBE";
}

TEST(OrbeBinaural, EnableMidStreamHasNoClick) {
    // Activar a mitad de stream no debe producir escalones: el salto
    // muestra a muestra tras el cambio debe ser < 5 % del pico (mismo
    // criterio que test_intelligent_upmixer).
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    o.setAmount(0.0f);
    // Tono de 100 Hz: su slew natural por muestra es 0.0065, muy por
    // debajo del umbral, así que cualquier salto mayor es un clic real
    // (un tono de 1 kHz a 0.5 ya cambia 0.065 por muestra por sí solo).
    const double fs = 48000.0;
    const int n = 48000 * 2;
    auto l = tone(100.0, 0.5, fs, n);
    auto r = tone(100.0, 0.5, fs, n, 0.7);
    runBlocks(o, l, r);  // primer segundo apagado (bit-exacto)
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    o.setParams(p);
    std::vector<float> l2(l.begin() + 48000, l.end()), r2(r.begin() + 48000, r.end());
    runBlocks(o, l2, r2);  // segundo segundo con ORBE activándose
    double peak = 0, jump = 0;
    for (size_t i = 0; i < l2.size(); ++i) {
        peak = std::max(peak, (double)std::fabs(l2[i]));
        if (i > 0) jump = std::max(jump, (double)std::fabs(l2[i] - l2[i - 1]));
    }
    EXPECT_LT(jump, 0.05 * peak + 1e-3) << "sin clic al activar";
}

TEST(OrbeBinaural, StableAndBoundedAtAllSampleRates) {
    for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f, 384000.0f}) {
        ivanna::OrbeBinaural o;
        o.prepare(sr);
        ivanna::OrbeParams p;
        p.amount = 1.0f;
        o.setParams(p);
        const int n = (int)sr;  // 1 s
        std::mt19937 g(3);
        std::uniform_real_distribution<float> u(-1, 1);
        std::vector<float> l(n), r(n);
        for (auto& v : l) v = u(g);
        for (auto& v : r) v = u(g);
        runBlocks(o, l, r);
        double e = 0.0;
        for (int i = 0; i < n; ++i) {
            ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i])) << sr;
            // Pico: la rotación de fase por bin redistribuye la energía y
            // el pico de ruido a escala completa puede subir ~1.6× (medido
            // 1.58 @384 kHz con esta semilla). 2.0 deja margen sin esconder
            // una ganancia real: antes del arreglo COLA el pico era 12.6.
            // El SafetyLimiter aguas abajo acota la salida del chain.
            ASSERT_LE(std::fabs(l[i]), 2.0f) << sr;
            ASSERT_LE(std::fabs(r[i]), 2.0f) << sr;
            e += (double)l[i] * l[i] + (double)r[i] * r[i];
        }
        // Potencia conservada: RMS de salida ≈ 0.46 con ruido ±1 (la
        // entrada es 0.577; el render mid/side con diffuse 0.5 la baja).
        const double rmsOut = std::sqrt(e / (2.0 * n));
        EXPECT_LE(rmsOut, 1.0) << sr;
        EXPECT_GT(rmsOut, 0.1) << sr;
        // La FFT escala con la SR para mantener ~10.7 ms de ventana.
        EXPECT_EQ(o.fftSize(), sr <= 48000 ? 512 : sr <= 96000 ? 1024 : sr <= 192000 ? 2048 : 4096) << sr;
    }
}

TEST(OrbeBinaural, SilenceDcAndNoiseStayFinite) {
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    o.setParams(p);
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    runBlocks(o, l, r);
    for (float v : l) ASSERT_EQ(v, 0.0f);
    for (float v : r) ASSERT_EQ(v, 0.0f);
    std::fill(l.begin(), l.end(), 0.5f);
    std::fill(r.begin(), r.end(), -0.5f);
    runBlocks(o, l, r);
    for (size_t i = 0; i < l.size(); ++i) {
        ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i]));
        ASSERT_LE(std::fabs(l[i]), 2.0f);
    }
}

TEST(OrbeBinaural, ThermalShedForcesBypass) {
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    o.setParams(p);
    o.setThermalShed(true);
    std::vector<float> l = tone(1000.0, 0.5, 48000.0, 48000);
    std::vector<float> r = tone(1000.0, 0.5, 48000.0, 48000, 0.5);
    auto l0 = l, r0 = r;
    runBlocks(o, l, r);
    EXPECT_EQ(l, l0);
    EXPECT_EQ(r, r0);
}

TEST(OrbeBinaural, InvalidParamsFallBackToSafeDefaults) {
    ivanna::OrbeBinaural o;
    o.prepare(48000.0f);
    ivanna::OrbeParams p;
    p.amount = NAN;
    p.distanceM = -5.0f;
    p.spread = 99.0f;
    p.proximityDb = -1.0f;
    o.setParams(p);
    std::vector<float> l = tone(1000.0, 0.5, 48000.0, 48000);
    std::vector<float> r = l;
    auto l0 = l;
    runBlocks(o, l, r);
    EXPECT_EQ(l, l0) << "amount NaN -> 0 = bypass bit-exacto";
    EXPECT_FLOAT_EQ(o.appliedAmount(), 0.0f);
}
