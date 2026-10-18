#pragma once
// ivanna_measure.hpp — métricas de audio reproducibles para el arnés de medición de host.
// Header-only, sin dependencias de Android. Todo se calcula en double.
//
// ALCANCE HONESTO: estas métricas se evalúan sobre el código C++ real (en host x86-64). Los números
// de CPU/tiempo de host NO son los del teléfono; la latencia en muestras SÍ es la del algoritmo.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ivanna::measure {

constexpr double kPi = 3.14159265358979323846;

inline double dB20(double x) { return 20.0 * std::log10(std::max(x, 1.0e-30)); }
inline double dB10(double x) { return 10.0 * std::log10(std::max(x, 1.0e-30)); }
inline double rms(const float* x, int n) {
    double s = 0; for (int i = 0; i < n; ++i) s += (double)x[i] * x[i];
    return std::sqrt(s / std::max(n, 1));
}

// Proyección LS sobre cos/sin a f (Hz). Con ciclos enteros en n es exacta y ortogonal a los demás.
inline void project(const float* x, int n, double fs, double f, double& a, double& b) {
    double sa = 0, sb = 0;
    for (int i = 0; i < n; ++i) {
        const double ph = 2.0 * kPi * f * i / fs;
        sa += x[i] * std::cos(ph); sb += x[i] * std::sin(ph);
    }
    a = 2.0 * sa / n; b = 2.0 * sb / n;
}

// THD+N [dB rel. a la fundamental]: residuo tras quitar DC y la fundamental ajustada.
// f0*n/fs debe ser entero (señal coherente). Devuelve también la amplitud ajustada.
struct Thdn { double thdnDb; double amp; };
inline Thdn thdn(const float* x, int n, double fs, double f0) {
    double mean = 0; for (int i = 0; i < n; ++i) mean += x[i]; mean /= n;
    std::vector<float> y(n);
    for (int i = 0; i < n; ++i) y[i] = (float)(x[i] - mean);
    double a, b; project(y.data(), n, fs, f0, a, b);
    double e = 0;
    for (int i = 0; i < n; ++i) {
        const double ph = 2.0 * kPi * f0 * i / fs;
        const double r = y[i] - (a * std::cos(ph) + b * std::sin(ph));
        e += r * r;
    }
    const double resRms = std::sqrt(e / n);
    const double amp = std::sqrt(a * a + b * b);
    return { dB20(resRms / std::max(amp / std::sqrt(2.0), 1.0e-30)), amp };
}

// IMD SMPTE (f1 baja, f2 alta; razón de amplitudes 4:1 típica): bandas laterales f2±k*f1 (k=1,2)
// respecto a la amplitud en f2. n debe contener ciclos enteros de ambas.
inline double imdSmpteDb(const float* x, int n, double fs, double f1, double f2) {
    double a2, b2; project(x, n, fs, f2, a2, b2);
    const double ref = std::sqrt(a2 * a2 + b2 * b2);
    double e = 0;
    for (int k = 1; k <= 2; ++k)
        for (int s : {-1, 1}) {
            double a, b; project(x, n, fs, f2 + s * k * f1, a, b);
            e += a * a + b * b;
        }
    return dB20(std::sqrt(e) / std::max(ref, 1.0e-30));
}

// True-peak (dBTP) por sobremuestreo 4x con interpolación sinc ventaneada (Hann, ±12 muestras).
inline double truePeakLin(const float* x, int n) {
    double pk = 0;
    for (int i = 0; i < n; ++i) pk = std::max(pk, (double)std::fabs(x[i]));
    for (int i = 12; i < n - 12; ++i)
        for (int p = 1; p < 4; ++p) {
            const double d = p / 4.0; double y = 0;
            for (int k = -11; k <= 12; ++k) {
                const double t = k - d;
                const double sinc = (std::fabs(t) < 1e-12) ? 1.0 : std::sin(kPi * t) / (kPi * t);
                const double w = 0.5 * (1.0 + std::cos(kPi * t / 12.5));
                y += x[i + k] * sinc * w;
            }
            pk = std::max(pk, std::fabs(y));
        }
    return pk;
}

// Sonoridad integrada BS.1770-4 (K-weighting a 48 kHz, bloques 400 ms / solape 75 %, gates -70 y -10 LU).
struct Biquad { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double run(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
inline double lufsIntegrated(const float* L, const float* R, int n, double fs = 48000.0) {
    if (n < (int)(0.4 * fs)) return -300.0;
    Biquad s1l{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585};
    Biquad s2l{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
    Biquad s1r = s1l, s2r = s2l;
    std::vector<double> kl(n), kr(n);
    for (int i = 0; i < n; ++i) { kl[i] = s2l.run(s1l.run(L[i])); kr[i] = s2r.run(s1r.run(R[i])); }
    const int blk = (int)(0.4 * fs), hop = (int)(0.1 * fs);
    std::vector<double> ms;
    for (int s = 0; s + blk <= n; s += hop) {
        double zl = 0, zr = 0;
        for (int i = s; i < s + blk; ++i) { zl += kl[i] * kl[i]; zr += kr[i] * kr[i]; }
        ms.push_back(zl / blk + zr / blk);
    }
    auto lk = [](double z) { return -0.691 + 10.0 * std::log10(std::max(z, 1e-30)); };
    double sum = 0; int cnt = 0;
    for (double z : ms) if (lk(z) > -70.0) { sum += z; ++cnt; }
    if (!cnt) return -300.0;
    const double gate = lk(sum / cnt) - 10.0;
    sum = 0; cnt = 0;
    for (double z : ms) if (lk(z) > gate) { sum += z; ++cnt; }
    return cnt ? lk(sum / cnt) : -300.0;
}

// Escalones de ganancia entre bloques consecutivos: ganancia por bloque = rms_out/rms_in [dB];
// devuelve el mayor salto entre bloques adyacentes y la desviación típica (pumping).
struct GainSteps { double maxStepDb; double stdDb; int blocks; };
inline GainSteps blockGainSteps(const float* in, const float* out, int n, int blk) {
    std::vector<double> g;
    for (int s = 0; s + blk <= n; s += blk) {
        const double ri = rms(in + s, blk), ro = rms(out + s, blk);
        if (ri > 1e-6) g.push_back(dB20(ro / ri));
    }
    GainSteps r{0, 0, (int)g.size()};
    if (g.size() < 2) return r;
    double m = 0; for (double v : g) m += v; m /= g.size();
    double v2 = 0; for (double v : g) v2 += (v - m) * (v - m);
    r.stdDb = std::sqrt(v2 / g.size());
    for (size_t i = 1; i < g.size(); ++i) r.maxStepDb = std::max(r.maxStepDb, std::fabs(g[i] - g[i - 1]));
    return r;
}

inline double nowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Escritor JSON mínimo (sin dependencias): objetos planos de pares clave/valor ya serializados.
struct Json {
    std::string s; bool first = true;
    void raw(const std::string& k, const std::string& v) { s += (first ? "" : ",\n  ") + ("\"" + k + "\": " + v); first = false; }
    void num(const std::string& k, double v) {
        char b[64]; std::snprintf(b, sizeof b, std::isfinite(v) ? "%.4f" : "null", v); raw(k, b);
    }
    void str(const std::string& k, const std::string& v) { raw(k, "\"" + v + "\""); }
    std::string done() const { return "{\n  " + s + "\n}\n"; }
};

} // namespace ivanna::measure
