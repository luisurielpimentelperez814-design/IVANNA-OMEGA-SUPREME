/**
 * test_differential_reinjector.cpp — Reinyección diferencial de Ruta A (v2.5.0)
 *
 * Simula al OYENTE de Ruta A: y(t) = x(t) + out(t − L), con x = original (0 ms, no
 * retrasable) y out = lo reinyectado por AudioTrack (retardo L ≈ 20-40 ms). Mide,
 * ANTES (v2.4.50: delta fijo g=0.6; v2.4.0: seco + 0.4·procesado) y DESPUÉS:
 *   1. profundidad de peine (rizado de |H(f)| a resolución 5.9 Hz, rms vs. suavizado 1/3 oct.)
 *   2. alineación temporal (retardo propio del DSP, fraccional) y delta residual
 *   3. estabilidad con distintas latencias L y jitter
 *   4. ausencia de clicks al cambiar parámetros / saltos de alineación
 *   5. conservación de graves (sin cancelación a L = múltiplos de medio periodo)
 *   6. mejora espacial sin eco: el delta decorrelado pasa íntegro
 *   7. cero asignaciones en process()
 * Cada medida se imprime ("[MEDIDA] ...") para que el CHANGELOG cite datos reales.
 */
#include <gtest/gtest.h>
#include "differential_reinjector.hpp"

#include <atomic>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

// ── contador de asignaciones (solo este ejecutable) ─────────────────────────
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
static std::atomic<long> g_allocs{0};
static std::atomic<bool> g_count{false};
// Familia COMPLETA de new/delete sobre malloc/free: reemplazar solo una parte
// mezcla la asignacion de ASan (new) con free() y dispara alloc-dealloc-mismatch.
static void* counted_alloc(std::size_t n) {
    if (g_count.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n ? n : 1);
}
static void* counted_alloc_aligned(std::size_t n, std::size_t al) {
    if (g_count.load(std::memory_order_relaxed)) g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (al < sizeof(void*)) al = sizeof(void*);
    void* p = nullptr;
    return posix_memalign(&p, al, n ? n : 1) == 0 ? p : nullptr;
}
void* operator new(std::size_t n) { if (void* p = counted_alloc(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (void* p = counted_alloc(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return counted_alloc(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return counted_alloc(n); }
void* operator new(std::size_t n, std::align_val_t a) {
    if (void* p = counted_alloc_aligned(n, static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) {
    if (void* p = counted_alloc_aligned(n, static_cast<std::size_t>(a))) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

using ivanna::DifferentialReinjector;

namespace {
constexpr float SR = 48000.f;
constexpr int   BLK = 320;                       // bloque real de PlaybackCaptureService

// ── FFT radix-2 + Welch ─────────────────────────────────────────────────────
using cd = std::complex<double>;
void fft(std::vector<cd>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / static_cast<double>(len);
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cd w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v; w *= wl;
            }
        }
    }
}
// |H(f)| en dB, H = Sxy/Sxx (promedio de Welch, Hann, 50 %)
std::vector<double> transferDb(const std::vector<float>& x, const std::vector<float>& y, int nfft = 8192) {
    std::vector<double> sxx(nfft / 2 + 1, 0.0);
    std::vector<cd> sxy(nfft / 2 + 1, cd(0));
    std::vector<double> win(nfft);
    for (int i = 0; i < nfft; ++i) win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / nfft);
    std::vector<cd> X(nfft), Y(nfft);
    for (size_t s = 0; s + nfft <= x.size(); s += nfft / 2) {
        for (int i = 0; i < nfft; ++i) { X[i] = x[s + i] * win[i]; Y[i] = y[s + i] * win[i]; }
        fft(X); fft(Y);
        for (int k = 0; k <= nfft / 2; ++k) { sxx[k] += std::norm(X[k]); sxy[k] += Y[k] * std::conj(X[k]); }
    }
    std::vector<double> h(nfft / 2 + 1);
    for (int k = 0; k <= nfft / 2; ++k) h[k] = 20.0 * std::log10(std::abs(sxy[k] / (sxx[k] + 1e-30)) + 1e-12);
    return h;
}
// Rizado de peine: rms de (dB − suavizado 1/3 oct.) en 500 Hz … 10 kHz
double combRmsDb(const std::vector<double>& h, int nfft = 8192) {
    const double df = SR / nfft;
    double acc = 0; int cnt = 0;
    for (int k = static_cast<int>(500 / df); k < static_cast<int>(10000 / df); ++k) {
        const double f = k * df;
        const int k0 = std::max(1, static_cast<int>(f / 1.12 / df)), k1 = static_cast<int>(f * 1.12 / df);
        double m = 0; for (int j = k0; j <= k1; ++j) m += h[j];
        m /= (k1 - k0 + 1);
        acc += (h[k] - m) * (h[k] - m); ++cnt;
    }
    return std::sqrt(acc / cnt);
}

// ── fuentes y modelos de DSP ────────────────────────────────────────────────
std::vector<float> noise(size_t n, uint32_t seed) {
    std::mt19937 g(seed); std::normal_distribution<float> d(0.f, 0.1f);
    std::vector<float> v(n); for (auto& s : v) s = d(g); return v;
}
// decorrelador: FIR ruidoso causal con h[0..LEN) y h[0]=0 → delta sin correlación con x a ningún retardo > LEN
std::vector<float> decorrelate(const std::vector<float>& x, uint32_t seed) {
    constexpr int LEN = 96;
    std::mt19937 g(seed); std::normal_distribution<float> d(0.f, 1.f);
    float h[LEN]; h[0] = 0;
    for (int i = 1; i < LEN; ++i) h[i] = d(g) * std::exp(-i / 30.f) * 0.18f;
    std::vector<float> y(x.size(), 0.f);
    for (size_t n = 0; n < x.size(); ++n)
        for (int k = 1; k < LEN && k <= static_cast<int>(n); ++k) y[n] += h[k] * x[n - k];
    return y;
}

// delta VARIANTE EN EL TIEMPO (decorrelación dinámica tipo chorus): x(t − τ(t)), τ = 40 ± 25 muestras @ 1.3 Hz.
// A diferencia de un FIR estacionario no es función lineal estable del original: no forma peine coherente.
std::vector<float> modulatedDelay(const std::vector<float>& x) {
    std::vector<float> y(x.size(), 0.f);
    for (size_t n = 70; n < x.size(); ++n) {
        const double tau = 40.0 + 25.0 * std::sin(2.0 * M_PI * 1.3 * n / SR);
        const double p = n - tau; const size_t i0 = static_cast<size_t>(p); const float f = static_cast<float>(p - i0);
        y[n] = x[i0] * (1.f - f) + x[i0 + 1] * f;
    }
    return y;
}

struct Run { std::vector<float> x, wet, out; DifferentialReinjector::Telemetry tel; };

// Ejecuta el reinyector bloque a bloque (estéreo duplicado). wet = función de x completa.
Run runReinjector(const std::vector<float>& x, const std::vector<float>& wet, float Lms, float intensity,
                  DifferentialReinjector& r) {
    Run run; run.x = x; run.wet = wet; run.out.assign(x.size(), 0.f);
    r.setListenerLatencyMs(Lms); r.setIntensity(intensity);
    std::vector<float> d(2 * BLK), w(2 * BLK), o(2 * BLK);
    for (size_t s = 0; s + BLK <= x.size(); s += BLK) {
        for (int i = 0; i < BLK; ++i) { d[2*i] = d[2*i+1] = x[s+i]; w[2*i] = w[2*i+1] = wet[s+i]; }
        r.process(d.data(), w.data(), o.data(), BLK);
        for (int i = 0; i < BLK; ++i) run.out[s+i] = o[2*i];
    }
    run.tel = r.telemetry();
    return run;
}
std::vector<float> listenerNew(const Run& r, int Ls) {            // y = x + out(t−L)
    std::vector<float> y(r.x);
    for (size_t n = Ls; n < y.size(); ++n) y[n] += r.out[n - Ls];
    return y;
}
std::vector<float> listenerDeltaOld(const Run& r, int Ls, float g) {   // v2.4.50: x + g·(wet−x)(t−L)
    std::vector<float> y(r.x);
    for (size_t n = Ls; n < y.size(); ++n) y[n] += g * (r.wet[n - Ls] - r.x[n - Ls]);
    return y;
}
std::vector<float> listenerHaasOld(const Run& r, int Ls) {            // v2.4.0: x + 0.4·wet(t−L)
    std::vector<float> y(r.x);
    for (size_t n = Ls; n < y.size(); ++n) y[n] += 0.40f * r.wet[n - Ls];
    return y;
}
int toSamples(float ms) { return static_cast<int>(std::lround(ms * 0.001f * SR)); }
double rmsOf(const std::vector<float>& v, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b; ++i) s += double(v[i]) * v[i]; return std::sqrt(s / (b - a));
}
double dbOf(double x) { return 20.0 * std::log10(x + 1e-12); }
std::vector<float> scaled(const std::vector<float>& x, float k) { std::vector<float> y(x); for (auto& s : y) s *= k; return y; }
std::vector<float> plusSeries(const std::vector<float>& a, const std::vector<float>& b, float kb) {
    std::vector<float> y(a); for (size_t i = 0; i < y.size(); ++i) y[i] += kb * b[i]; return y;
}
} // namespace

// ═══ 1. Reducción del filtrado peine (ANTES / DESPUÉS) ═════════════════════
// Peor caso real: el compresor/limitador atenúa → delta = −k·x (anticorrelado con x al retardo L).
TEST(DifferentialReinjector, ReducesCombFilteringWorstCase) {
    const size_t N = static_cast<size_t>(SR) * 20;
    const auto x = noise(N, 1);
    const auto wet = scaled(x, 0.6f);                     // limitador/compresor atenuando
    for (float Lms : {20.f, 30.f, 40.f}) {
        DifferentialReinjector r; r.prepare(SR, BLK);
        const auto run = runReinjector(x, wet, Lms, 0.6f, r);
        const int Ls = toSamples(Lms);
        auto cut = [&](const std::vector<float>& v) { return std::vector<float>(v.begin() + 48000, v.end()); };
        const double haas  = combRmsDb(transferDb(cut(x), cut(listenerHaasOld(run, Ls))));
        const double dOld  = combRmsDb(transferDb(cut(x), cut(listenerDeltaOld(run, Ls, 0.6f))));
        const double dNew  = combRmsDb(transferDb(cut(x), cut(listenerNew(run, Ls))));
        std::printf("[MEDIDA] peine rms (dB) L=%2.0f ms  v2.4.0 Haas=%.2f  v2.4.50 delta=%.2f  v2.5.0 diferencial=%.2f  riesgo=%.3f\n",
                    Lms, haas, dOld, dNew, run.tel.combRisk);
        EXPECT_GT(dOld, 1.0) << "el caso peor debe reproducir el peine anterior";
        EXPECT_LT(dNew, 0.45);
        EXPECT_LT(dNew, 0.45 * dOld);
        EXPECT_LE(run.tel.combRisk, DifferentialReinjector::kRiskMax + 0.03f);
    }
}

// ═══ 2. Alineación temporal correcta (latencia propia del DSP, fraccional) ═══
static std::vector<float> sines(size_t n, double delay, double gain = 1.0) {
    const double f[4] = {200, 700, 1700, 2900};
    std::vector<float> y(n);
    for (size_t i = 0; i < n; ++i) {
        double s = 0; for (double fr : f) s += std::sin(2.0 * M_PI * fr * (static_cast<double>(i) - delay) / SR);
        y[i] = static_cast<float>(0.1 * gain * s);
    }
    return y;
}
TEST(DifferentialReinjector, EstimatesFractionalDspLagAndCancelsResidualDelta) {
    for (double D : {0.0, 3.0, 7.4, 21.75}) {
        const size_t N = static_cast<size_t>(SR) * 6;
        const auto x = sines(N, 0.0), wet = sines(N, D);
        DifferentialReinjector r; r.prepare(SR, BLK);
        const auto run = runReinjector(x, wet, 30.f, 1.0f, r);
        const double lag = run.tel.dspLagSamples;
        const double resid = dbOf(rmsOf(run.out, N - 48000, N)) - dbOf(rmsOf(wet, N - 48000, N));
        std::printf("[MEDIDA] alineacion D=%.2f muestras -> estimado=%.3f  delta residual=%.1f dB re wet\n", D, lag, resid);
        EXPECT_NEAR(lag, D, 0.3);
        EXPECT_LT(resid, -20.0) << "con DSP = solo latencia, el delta alineado debe anularse";
    }
}

// ═══ 3. Estabilidad con distintas latencias y jitter ═════════════════════════
TEST(DifferentialReinjector, StableAcrossLatenciesAndJitter) {
    const size_t N = static_cast<size_t>(SR) * 10;
    const auto x = noise(N, 7), wet = scaled(x, 0.6f);
    for (float Lms : {8.f, 15.f, 25.f, 40.f, 80.f, 150.f}) {
        DifferentialReinjector r; r.prepare(SR, BLK);
        std::mt19937 g(5); std::uniform_real_distribution<float> jit(-3.f, 3.f);
        std::vector<float> d(2 * BLK), w(2 * BLK), o(2 * BLK);
        float peak = 0, risk = 0;
        for (size_t s = 0; s + BLK <= N; s += BLK) {
            r.setListenerLatencyMs(Lms + jit(g));                    // jitter ±3 ms por bloque
            for (int i = 0; i < BLK; ++i) { d[2*i] = d[2*i+1] = x[s+i]; w[2*i] = w[2*i+1] = wet[s+i]; }
            r.process(d.data(), w.data(), o.data(), BLK);
            for (float v : o) { ASSERT_TRUE(std::isfinite(v)); peak = std::max(peak, std::fabs(v)); }
            if (s > static_cast<size_t>(SR) * 2) risk = std::max(risk, r.telemetry().combRisk);
        }
        std::printf("[MEDIDA] estabilidad L=%.0f±3 ms  pico delta=%.3f  riesgo max=%.3f\n", Lms, peak, risk);
        EXPECT_LT(peak, 0.8f);
        EXPECT_LE(risk, DifferentialReinjector::kRiskMax + 0.06f);
    }
}

// ═══ 4. Ausencia de clicks al cambiar parámetros y en saltos de alineación ═══
static double maxStepRatio(const std::vector<float>& out, size_t from, double f) {
    double pk = 0, st = 0;
    for (size_t i = from; i < out.size(); ++i) pk = std::max(pk, double(std::fabs(out[i])));
    for (size_t i = from + 1; i < out.size(); ++i) st = std::max(st, double(std::fabs(out[i] - out[i - 1])));
    const double bound = 2.0 * M_PI * f / SR * pk;                    // pendiente máxima de un seno de pico `pk`
    return bound > 0 ? st / bound : 0;
}
TEST(DifferentialReinjector, NoClicksOnParameterChanges) {
    const size_t N = static_cast<size_t>(SR) * 4;
    std::vector<float> x(N), wet(N);
    for (size_t i = 0; i < N; ++i) { x[i] = 0.3f * std::sin(2.0 * M_PI * 1000.0 * i / SR); wet[i] = 0.18f * std::sin(2.0 * M_PI * 1000.0 * i / SR + 0.3); }
    DifferentialReinjector r; r.prepare(SR, BLK);
    std::vector<float> d(2 * BLK), w(2 * BLK), o(2 * BLK), out(N, 0.f);
    int blk = 0;
    for (size_t s = 0; s + BLK <= N; s += BLK, ++blk) {
        if (blk == 300) r.setIntensity(0.05f);                       // bajada brusca
        if (blk == 330) r.setIntensity(0.95f);                       // subida brusca
        if (blk == 360) r.setListenerLatencyMs(42.f);                // salto de latencia
        if (blk == 390) r.setBandCaps(1.f, 0.1f, 0.5f);              // cambio de topes por banda
        if (blk == 420) r.setContentHint(1.f, 1.f);                  // contenido: voz tonal
        for (int i = 0; i < BLK; ++i) { d[2*i] = d[2*i+1] = x[s+i]; w[2*i] = w[2*i+1] = wet[s+i]; }
        r.process(d.data(), w.data(), o.data(), BLK);
        for (int i = 0; i < BLK; ++i) out[s + i] = o[2*i];
    }
    const double ratio = maxStepRatio(out, 20000, 1000.0);
    std::printf("[MEDIDA] clicks: paso max / limite de pendiente = %.3f (click => > 1.5)\n", ratio);
    EXPECT_LT(ratio, 1.5);
}
TEST(DifferentialReinjector, NoClicksOnAlignmentJump) {
    const size_t N = static_cast<size_t>(SR) * 6;
    const auto x = sines(N, 0.0);
    std::vector<float> wet(N);
    const auto a = sines(N, 3.0), b = sines(N, 40.5);
    for (size_t i = 0; i < N; ++i) wet[i] = (i < N / 2) ? a[i] : b[i];  // el DSP cambia de latencia 3 → 40.5 muestras
    DifferentialReinjector r; r.prepare(SR, BLK);
    const auto run = runReinjector(x, wet, 30.f, 1.0f, r);
    const double ratio = maxStepRatio(run.out, 20000, 2900.0);
    std::printf("[MEDIDA] salto de alineacion 3 -> 40.5: paso max / limite = %.3f  lag final=%.2f\n", ratio, run.tel.dspLagSamples);
    EXPECT_NEAR(run.tel.dspLagSamples, 40.5, 0.5);
    EXPECT_LT(ratio, 2.0);
}

// ═══ 5. Conservación de graves (sin cancelación a L = medios periodos) ═══════
TEST(DifferentialReinjector, PreservesBassAtCancellingLatencies) {
    const size_t N = static_cast<size_t>(SR) * 8;
    std::vector<float> x(N);
    for (size_t i = 0; i < N; ++i) x[i] = 0.3f * std::sin(2.0 * M_PI * 80.0 * i / SR);   // periodo 12.5 ms
    const auto wet = scaled(x, 0.6f);
    double worstOld = 0, worstNew = 0;
    for (float Lms : {25.f, 31.25f, 37.5f, 43.75f}) {                // 2, 2.5, 3, 3.5 periodos
        DifferentialReinjector r; r.prepare(SR, BLK);
        const auto run = runReinjector(x, wet, Lms, 0.6f, r);
        const int Ls = toSamples(Lms);
        const double ref = rmsOf(x, N - 48000, N);
        const double dOld = std::fabs(dbOf(rmsOf(listenerDeltaOld(run, Ls, 0.6f), N - 48000, N) / ref));
        const double dNew = std::fabs(dbOf(rmsOf(listenerNew(run, Ls), N - 48000, N) / ref));
        std::printf("[MEDIDA] graves 80 Hz L=%.2f ms: desviacion v2.4.50=%.2f dB  v2.5.0=%.2f dB\n", Lms, dOld, dNew);
        worstOld = std::max(worstOld, dOld); worstNew = std::max(worstNew, dNew);
    }
    EXPECT_GT(worstOld, 1.5);
    EXPECT_LT(worstNew, 1.0);
}

// ═══ 6. Mejora espacial sin eco ═════════════════════════════════════════════
// Física (medida): un delta que es función estable del original forma peine al sumarse con retardo L
// aunque no esté "correlacionado" en el tiempo → el guardián mide la ganancia coherente y la limita.
// Solo la parte INCOHERENTE (información que no es función del original) pasa íntegra y no forma peine.
static double transmittedDb(const Run& r, size_t from) {
    double want = 0, got = 0;
    for (size_t i = from; i < r.x.size(); ++i) { const double d = r.wet[i] - r.x[i]; want += d * d; got += double(r.out[i]) * r.out[i]; }
    return 10.0 * std::log10(got / want);
}
TEST(DifferentialReinjector, SpatialDeltaBoundedByCoherenceIncoherentPasses) {
    const size_t N = static_cast<size_t>(SR) * 20;
    const auto x = noise(N, 11);
    const auto wetLin = plusSeries(x, decorrelate(x, 3), 1.0f);       // espacial coherente (FIR estacionario)
    const auto wetDyn = plusSeries(x, modulatedDelay(x), 0.6f);       // espacial variante en el tiempo (chorus)
    const auto wetInc = plusSeries(x, noise(N, 99), 0.4f);            // información NUEVA no derivable del original
    const int Ls = toSamples(30.f);
    auto cut = [&](const std::vector<float>& v) { return std::vector<float>(v.begin() + 48000, v.end()); };
    double pass[3], comb[3], combFull[3]; const char* nm[3] = {"lineal", "chorus", "incoherente"};
    const std::vector<float>* ws[3] = {&wetLin, &wetDyn, &wetInc};
    for (int k = 0; k < 3; ++k) {
        DifferentialReinjector r; r.prepare(SR, BLK);
        const auto run = runReinjector(x, *ws[k], 30.f, 0.8f, r);
        pass[k] = transmittedDb(run, 48000);
        comb[k] = combRmsDb(transferDb(cut(x), cut(listenerNew(run, Ls))));
        combFull[k] = combRmsDb(transferDb(cut(x), cut(listenerDeltaOld(run, Ls, 0.8f))));
        std::printf("[MEDIDA] espacial %-11s: delta transmitido=%6.1f dB  peine v2.4.50=%.2f dB -> v2.5.0=%.2f dB\n", nm[k], pass[k], combFull[k], comb[k]);
        EXPECT_LT(comb[k], 0.5) << nm[k];
    }
    EXPECT_GT(pass[2], pass[0] + 8.0) << "la informacion incoherente debe pasar mucho mas que la coherente";
    EXPECT_GT(pass[2], -12.0);
}

// ═══ 7. Inmersión perceptual: sube con contexto seguro, baja con riesgo/latencia ═
TEST(DifferentialReinjector, ImmersionTracksContext) {
    const size_t N = static_cast<size_t>(SR) * 6;
    const auto x = noise(N, 21);
    const auto fresh = plusSeries(x, noise(N, 5), 0.4f), correlated = scaled(x, 0.6f);
    auto imm = [&](const std::vector<float>& wet, float L, float speech = 0.f) {
        DifferentialReinjector r; r.prepare(SR, BLK); r.setContentHint(speech, 0.f);
        return runReinjector(x, wet, L, 0.8f, r).tel.immersionCeiling;
    };
    const float f10 = imm(fresh, 10.f), f40 = imm(fresh, 40.f), c40 = imm(correlated, 40.f), f40v = imm(fresh, 40.f, 1.f), f40b = imm(fresh, 40.f);
    std::printf("[MEDIDA] inmersion techo: nuevo@10ms=%.2f nuevo@40ms=%.2f coherente@40ms=%.2f nuevo@40ms+voz=%.2f\n", f10, f40, c40, f40v);
    EXPECT_GT(f10, f40 + 0.1f);                  // más latencia → menos inmersión permitida
    EXPECT_GT(f40, c40 + 0.1f);                  // contenido coherente con el original → protege
    EXPECT_LT(f40v, f40 - 0.05f);                // voz → protege inteligibilidad
    EXPECT_FLOAT_EQ(f40, f40b);                  // determinista
}

// ═══ 8. Entradas hostiles y tiempo real ═════════════════════════════════════
TEST(DifferentialReinjector, SurvivesNaNInfAndHugeBlocks) {
    DifferentialReinjector r; r.prepare(SR, BLK);
    std::vector<float> d(2 * 4096, 0.2f), w(2 * 4096, 0.1f), o(2 * 4096);
    d[10] = NAN; w[20] = INFINITY; d[30] = -INFINITY;
    r.process(d.data(), w.data(), o.data(), 4096);                  // > maxFrames: se recorta, sin desbordar
    for (int i = 0; i < 2 * BLK; ++i) EXPECT_TRUE(std::isfinite(o[i]));
    r.process(nullptr, nullptr, o.data(), 0);                       // 0 frames: no-op
    DifferentialReinjector unprepared;
    unprepared.process(d.data(), w.data(), o.data(), 64);           // sin prepare: silencio, sin crash
    EXPECT_EQ(o[5], 0.f);
}
TEST(DifferentialReinjector, ProcessDoesNotAllocate) {
    const auto x = noise(BLK * 400, 31);
    DifferentialReinjector r; r.prepare(SR, BLK);
    std::vector<float> d(2 * BLK), w(2 * BLK), o(2 * BLK);
    g_allocs = 0; g_count = true;
    for (int b = 0; b < 400; ++b) {
        for (int i = 0; i < BLK; ++i) { d[2*i] = d[2*i+1] = x[b*BLK+i]; w[2*i] = w[2*i+1] = 0.6f * x[b*BLK+i]; }
        r.setIntensity(0.3f + 0.001f * b);
        r.process(d.data(), w.data(), o.data(), BLK);
    }
    g_count = false;
    EXPECT_EQ(g_allocs.load(), 0) << "process() no puede asignar memoria";
}
TEST(DifferentialReinjector, TransparentWhenIntensityZero) {
    const auto x = noise(BLK * 200, 41);
    DifferentialReinjector r; r.prepare(SR, BLK); r.setIntensity(0.f);
    std::vector<float> d(2 * BLK), w(2 * BLK), o(2 * BLK);
    float peak = 0;
    for (int b = 0; b < 200; ++b) {
        for (int i = 0; i < BLK; ++i) { d[2*i] = d[2*i+1] = x[b*BLK+i]; w[2*i] = w[2*i+1] = 0.6f * x[b*BLK+i]; }
        r.process(d.data(), w.data(), o.data(), BLK);
        if (b > 5) for (float v : o) peak = std::max(peak, std::fabs(v));
    }
    EXPECT_LT(peak, 1e-6f) << "intensidad 0 => no se reinyecta nada (el original suena intacto)";
}
