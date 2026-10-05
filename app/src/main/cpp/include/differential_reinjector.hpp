/**
 * differential_reinjector.hpp — Reinyección diferencial de Ruta A (v2.5.0)
 *
 * PROBLEMA (AGENT_CLAIMS, hallazgo 2): en Ruta A el stream original NO se puede
 * silenciar ni retrasar (MediaProjection captura una copia; el original sigue su
 * camino por AudioFlinger a 0 ms). Lo reinyectado por AudioTrack llega al oyente
 * con L ≈ 20-40 ms de retardo. El oyente oye:
 *
 *        y(t) = x(t) + g · delta(t − L)          delta = wet − dry
 *
 * Si delta está correlacionada con x (p.ej. el compresor/limitador atenúa:
 * delta = −k·x), la suma es un filtro de peine con teeth cada 1/L Hz.
 * Ningún "delay fijo" lo arregla: x no se puede alinear (no se puede retrasar el
 * original). Lo que SÍ se puede controlar es CUÁNTA parte de delta es coherente
 * con x al retardo L, banda por banda. Esa es la causa raíz que ataca esta clase.
 *
 * MODELO (medido, no supuesto): el oyente recibe
 *        Y(f) = X(f) · [ 1 + g · H(f) · e^{-jωL} ],   H(f) = Δ(f)/X(f) coherente
 * (la parte de delta que es función lineal estable del original al retardo L).
 * El rizado de peine pico a pico es 20·log10((1+g|H|)/(1−g|H|)): depende de la
 * GANANCIA COHERENTE |H|, no del nivel total. (Un primer diseño con correlación
 * temporal ρ NO bastaba: para ruido estacionario ρ(L)=0 y el peine existe igual;
 * lo detectaron los tests de este archivo.) La parte de delta incoherente
 * (variante en el tiempo / no lineal: armónicos, decorrelación dinámica) suma en
 * potencia y no forma peine: pasa íntegra.
 * Estimación: espectro cruzado Δ·X* y autoespectros con promedio exponencial
 * (FFT 8192 + truco real-compleja, hop 2048), emparejando x(t) con delta(t−L);
 * coherencia γ² con corrección de sesgo 1/M y del solape de ventanas (x(t) y
 * delta(t−L) comparten solo la fracción ξ(L) de la ventana Hann; sin dividir por ξ²
 * la coherencia real se subestima — lo midió el propio test: 0.18 en vez de 0.64).
 * Si ξ < 0.3 (L ≳ 0.5·ventana) no hay resolución: se asume coherente. Por banda: c_b = 1.6·rms(|H|)
 * (factor de cresta de Rayleigh) y el guardián exige g_b·c_b ≤ 10^{R/20} − 1
 * (R = kRippleDb). Durante el arranque (sin estimación) asume el peor caso.
 *
 * ALINEACIÓN: la referencia seca usada para delta = wet − dry se alinea con wet
 * (latencia propia del DSP) con estimación por correlación, interpolación
 * fraccional Lagrange-3, corrección con slew gradual y crossfade en saltos.
 *
 * TIEMPO REAL: process() no asigna memoria, no bloquea, no usa excepciones.
 * Toda la memoria se reserva en prepare() (hilo no-RT). Controles = atomics.
 */
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ivanna {

class DifferentialReinjector {
public:
    static constexpr int   kBands      = 3;
    static constexpr int   kRingSize   = 32768;          // pot. de 2: 683 ms @ 48 kHz (ventana FFT 170 ms + L ≤ 250 ms)
    static constexpr int   kRingMask   = kRingSize - 1;
    static constexpr int   kMaxDspLag  = 192;            // 4 ms @ 48 kHz: latencia propia del DSP
    static constexpr int   kLagEvery   = 4;              // estimar 1 de cada 4 bloques
    static constexpr int   kXfSamples  = 240;            // crossfade de realineación (5 ms)
    static constexpr int   kFft        = 8192;           // 170 ms: resuelve dientes de 25 Hz y mantiene solape x/delta(t−L)
    static constexpr int   kHop        = 2048;
    static constexpr float kRippleDb   = 0.8f;           // rizado de peine pico tolerado por banda (dB)
    static constexpr int   kWarmHops   = 6;              // análisis mínimos antes de confiar en la estimación
    static constexpr float kRiskMax    = 0.0965f;        // = 10^(kRippleDb/20) − 1 (profundidad lineal tolerada)
    static constexpr float kSlewPerSmp = 5.0e-4f;        // muestras de retardo por muestra (0.05 % tono)

    struct Telemetry {
        float listenerLatencyMs = 0.f;
        float dspLagSamples     = 0.f;   // retardo propio del DSP estimado (fraccional)
        float lagConfidence     = 0.f;   // 0..1
        float corr[kBands]      = {0, 0, 0};   // ganancia coherente c_b = 1.6·rms|H| (delta↔seco a retardo L)
        float deltaToDry[kBands]= {0, 0, 0};   // r = σ_delta/σ_dry por banda
        float gain[kBands]      = {0, 0, 0};   // ganancia efectiva aplicada por banda
        float combRisk          = 0.f;   // max_b g_b·c_b: profundidad lineal de peine con la ganancia aplicada
        float immersion         = 0.f;   // intensidad perceptual efectiva 0..1
        float immersionCeiling  = 0.f;   // techo permitido por el contexto 0..1
    };

    DifferentialReinjector() = default;
    DifferentialReinjector(const DifferentialReinjector&) = delete;
    DifferentialReinjector& operator=(const DifferentialReinjector&) = delete;

    // ── Preparación (NO RT: reserva memoria) ────────────────────────────────
    void prepare(float sampleRate, int maxFrames = 1024) {
        sr_        = sampleRate > 8000.f ? sampleRate : 48000.f;
        maxFrames_ = std::clamp(maxFrames, 64, 4096);
        mem_.assign(static_cast<size_t>(4) * kRingSize, 0.f);
        float* p = mem_.data();
        for (int c = 0; c < 2; ++c, p += kRingSize) dryRing_[c] = p;
        xMono_ = p; p += kRingSize;          // seco mono (L+R)/2
        dMono_ = p;                          // delta mono sin ganancia
        fftRe_.assign(kFft, 0.f); fftIm_.assign(kFft, 0.f); win_.assign(kFft, 0.f);
        cosT_.assign(kFft / 2, 0.f); sinT_.assign(kFft / 2, 0.f); rev_.assign(kFft, 0);
        sxx_.assign(kFft / 2 + 1, 0.f); sdd_.assign(kFft / 2 + 1, 0.f);
        sdxR_.assign(kFft / 2 + 1, 0.f); sdxI_.assign(kFft / 2 + 1, 0.f);
        for (int n = 0; n < kFft; ++n) win_[n] = 0.5f - 0.5f * static_cast<float>(std::cos(2.0 * M_PI * n / kFft));
        for (int k = 0; k < kFft / 2; ++k) {
            cosT_[k] = static_cast<float>(std::cos(2.0 * M_PI * k / kFft));
            sinT_[k] = static_cast<float>(-std::sin(2.0 * M_PI * k / kFft));
        }
        for (int i = 0, j = 0; i < kFft; ++i) {
            rev_[i] = j;
            int bit = kFft >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
        }
        bandOf_.clear();
        for (int k = 0; k <= kFft / 2; ++k) {
            const float f = static_cast<float>(k) * sr_ / kFft;
            bandOf_.push_back(f < 180.f ? 0 : (f < 3500.f ? 1 : 2));
        }
        lagWet_.assign(static_cast<size_t>(maxFrames_), 0.f);
        lagDry_.assign(static_cast<size_t>(maxFrames_ + kMaxDspLag + 8), 0.f);
        lagCorr_.assign(static_cast<size_t>(kMaxDspLag + 2), 0.f);
        designFilters();
        blockSeconds_ = 0.f;
        prepared_ = true;
        reset();
    }

    // Limpia estado (NO RT). Los controles atómicos se conservan.
    void reset() {
        if (!prepared_) return;
        std::fill(mem_.begin(), mem_.end(), 0.f);
        pos_ = 0; lagCounter_ = 0;
        dCur_ = 0.0; dTarget_ = 0.0; dOld_ = 0.0; xf_ = 0;
        lagConf_ = 0.f;
        for (int c = 0; c < 2; ++c) { clearState(fDelta_[c]); clearState(fDry_[c]); }
        std::fill(sxx_.begin(), sxx_.end(), 0.f); std::fill(sdd_.begin(), sdd_.end(), 0.f);
        std::fill(sdxR_.begin(), sdxR_.end(), 0.f); std::fill(sdxI_.begin(), sdxI_.end(), 0.f);
        hopCount_ = 0; analyses_ = 0;
        for (int b = 0; b < kBands; ++b) { gCur_[b] = gNext_[b] = 0.f; coh_[b] = 1.f; ratio_[b] = 0.f; }
        ceilCur_ = 0.f; ceilNext_ = 0.f;
        transFast_[0] = transFast_[1] = 0.f;
        transSlow_[0] = transSlow_[1] = 0.f;
    }

    // ── Controles (cualquier hilo, lock-free) ───────────────────────────────
    void setIntensity(float v)          { intensity_.store(clamp01(v), std::memory_order_relaxed); }
    void setListenerLatencyMs(float ms) { latencyMs_.store(std::clamp(ms, 1.f, 250.f), std::memory_order_relaxed); }
    // speechProb 0..1 (voz → proteger inteligibilidad), tonality 0..1 (tonal = más sensible al peine)
    void setContentHint(float speechProb, float tonality) {
        speech_.store(clamp01(speechProb), std::memory_order_relaxed);
        tonal_.store(clamp01(tonality), std::memory_order_relaxed);
    }
    // Techo por banda (graves/medios/agudos). Graves conservador por diseño.
    void setBandCaps(float low, float mid, float high) {
        cap_[0].store(clamp01(low),  std::memory_order_relaxed);
        cap_[1].store(clamp01(mid),  std::memory_order_relaxed);
        cap_[2].store(clamp01(high), std::memory_order_relaxed);
    }
    float intensity() const { return intensity_.load(std::memory_order_relaxed); }

    Telemetry telemetry() const {
        Telemetry t;
        t.listenerLatencyMs = latencyMs_.load(std::memory_order_relaxed);
        t.dspLagSamples = tDspLag_.load(std::memory_order_relaxed);
        t.lagConfidence = tConf_.load(std::memory_order_relaxed);
        for (int b = 0; b < kBands; ++b) {
            t.corr[b]       = tCorr_[b].load(std::memory_order_relaxed);
            t.deltaToDry[b] = tRatio_[b].load(std::memory_order_relaxed);
            t.gain[b]       = tGain_[b].load(std::memory_order_relaxed);
        }
        t.combRisk         = tRisk_.load(std::memory_order_relaxed);
        t.immersion        = tImm_.load(std::memory_order_relaxed);
        t.immersionCeiling = tCeil_.load(std::memory_order_relaxed);
        return t;
    }

    // ── RT: dry/wet/out estéreo intercalado, mismo tamaño de bloque ─────────
    // out = Σ_b g_b · delta_b  (SOLO delta; el original ya suena por su vía).
    void process(const float* dry, const float* wet, float* out, int frames) {
        if (!prepared_ || frames <= 0) {
            if (out && frames > 0) std::memset(out, 0, sizeof(float) * 2 * static_cast<size_t>(frames));
            return;
        }
        if (frames > maxFrames_) frames = maxFrames_;
        const uint32_t base = pos_;
        blockSeconds_ = static_cast<float>(frames) / sr_;

        // 1) historial seco
        for (int i = 0; i < frames; ++i) {
            const uint32_t idx = (base + static_cast<uint32_t>(i)) & kRingMask;
            dryRing_[0][idx] = fin(dry[2 * i]);
            dryRing_[1][idx] = fin(dry[2 * i + 1]);
        }

        // 2) latencia propia del DSP (correlación wet vs dry)
        estimateDspLag(wet, frames, base);

        // 3) parámetros del bloque
        int Ls = static_cast<int>(std::lround(latencyMs_.load(std::memory_order_relaxed) * 0.001f * sr_));
        Ls = std::clamp(Ls, 1, kRingSize - maxFrames_ - 8);
        const float invN = 1.f / static_cast<float>(frames);

        for (int i = 0; i < frames; ++i) {
            // ── alineación: slew gradual o crossfade en saltos ──
            advanceDelay();
            const float a = static_cast<float>(i) * invN;
            float aligned[2];
            for (int c = 0; c < 2; ++c) {
                float v = readDry(c, base, i, frames, dCur_);
                if (xf_ > 0) {
                    const float w = static_cast<float>(xf_) / static_cast<float>(kXfSamples);
                    v = v * (1.f - w) + readDry(c, base, i, frames, dOld_) * w;
                }
                aligned[c] = v;
            }
            if (xf_ > 0) --xf_;

            const uint32_t wi = (base + static_cast<uint32_t>(i)) & kRingMask;
            float o[2] = {0.f, 0.f}, dsum = 0.f;
            for (int c = 0; c < 2; ++c) {
                const float delta = fin(wet[2 * i + c]) - aligned[c];
                dsum += delta;

                // FASE 3: Separación inteligente entre información original (timbre, ataques, dinámica)
                // e información reconstruida (espacio, profundidad, ambiente, microdetalle).
                // En transitorios rápidos, la reinyección se atenúa para no colorear el ataque seco (0 ms).
                const float absDry = std::fabs(aligned[c]);
                transFast_[c] += 0.08f * (absDry - transFast_[c]);
                transSlow_[c] += 0.005f * (absDry - transSlow_[c]);
                const float attackDiff = std::max(0.0f, transFast_[c] - transSlow_[c]);
                const float attackMask = std::clamp(attackDiff * 5.0f, 0.0f, 1.0f);
                const float transientPreserve = 1.0f - 0.70f * attackMask;

                float bandsDelta[kBands];
                split(fDelta_, c, delta, bandsDelta);
                for (int b = 0; b < kBands; ++b) {
                    const float g = gCur_[b] + (gNext_[b] - gCur_[b]) * a;   // rampa por muestra
                    const float bandMod = (b == 0) ? (transientPreserve * 0.90f) :
                                          (b == 1) ? transientPreserve :
                                                     (1.0f - 0.30f * attackMask);
                    o[c] += g * bandsDelta[b] * bandMod;
                }
            }
            xMono_[wi] = 0.5f * (dryRing_[0][wi] + dryRing_[1][wi]);
            dMono_[wi] = 0.5f * dsum;
            out[2 * i]     = o[0];
            out[2 * i + 1] = o[1];
        }
        pos_ = base + static_cast<uint32_t>(frames);
        hopCount_ += frames;
        if (hopCount_ >= kHop) { hopCount_ -= kHop; analyzeCoherence(Ls); }

        // 4) nuevas ganancias para el bloque siguiente (estado coherente cacheado)
        for (int b = 0; b < kBands; ++b) gCur_[b] = gNext_[b];
        ceilCur_ = ceilNext_;
        updateGains();
    }

private:
    // ── utilidades ──────────────────────────────────────────────────────────
    static float clamp01(float v) { return std::isfinite(v) ? std::clamp(v, 0.f, 1.f) : 0.f; }
    static float fin(float v)     { return std::isfinite(v) ? v : 0.f; }
    static float smoothstep(float e0, float e1, float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // Butterworth 4º orden = 2 biquads (Q 0.5412 y 1.3066), pasa-bajos.
    struct Biquad { double b0=1,b1=0,b2=0,a1=0,a2=0, z1=0,z2=0;
        inline float run(float x) {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            if (std::fabs(z1) < 1e-30) z1 = 0;
            if (std::fabs(z2) < 1e-30) z2 = 0;
            return static_cast<float>(y);
        } };
    struct Chain { Biquad lp1[2]; Biquad lp2[2]; };   // lp1: f1, lp2: f2

    void designFilters() {
        const double qs[2] = {0.5411961, 1.3065630};
        const double f[2]  = {180.0, 3500.0};
        Biquad proto[2][2];
        for (int k = 0; k < 2; ++k)
            for (int s = 0; s < 2; ++s) {
                const double w0 = 2.0 * M_PI * f[k] / sr_;
                const double cw = std::cos(w0), al = std::sin(w0) / (2.0 * qs[s]);
                const double a0 = 1.0 + al;
                Biquad q;
                q.b0 = (1.0 - cw) * 0.5 / a0; q.b1 = (1.0 - cw) / a0; q.b2 = q.b0;
                q.a1 = -2.0 * cw / a0;         q.a2 = (1.0 - al) / a0;
                proto[k][s] = q;
            }
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 2; ++s) {
                fDelta_[c].lp1[s] = fDry_[c].lp1[s] = proto[0][s];
                fDelta_[c].lp2[s] = fDry_[c].lp2[s] = proto[1][s];
            }
    }
    static void clearState(Chain& ch) {
        for (int s = 0; s < 2; ++s) { ch.lp1[s].z1 = ch.lp1[s].z2 = 0; ch.lp2[s].z1 = ch.lp2[s].z2 = 0; }
    }

    // Reconstrucción exacta: low = LP1(x); resto = x − low; mid = LP2(resto); high = resto − mid.
    // Σ bandas = x EXACTO (con ganancias 1 el delta pasa bit-a-bit salvo redondeo).
    void split(Chain (&bank)[2], int c, float x, float* bands) {
        Chain& ch = bank[c];
        float low = x;
        for (int s = 0; s < 2; ++s) low = ch.lp1[s].run(low);
        const float rest = x - low;
        float mid = rest;
        for (int s = 0; s < 2; ++s) mid = ch.lp2[s].run(mid);
        bands[0] = low; bands[1] = mid; bands[2] = rest - mid;
    }

    // Lagrange cúbico sobre el historial seco; retardo d (muestras, ≥0) respecto a la muestra i del bloque.
    float readDry(int c, uint32_t base, int i, int frames, double d) const {
        if (d < 1e-4) return dryRing_[c][(base + static_cast<uint32_t>(i)) & kRingMask];
        const double p  = static_cast<double>(i) - d;
        const double ip = std::floor(p);
        const float  fr = static_cast<float>(p - ip);
        const int    n0 = static_cast<int>(ip);
        auto tap = [&](int n) {
            if (n > frames - 1) n = frames - 1;     // no hay "futuro" más allá del bloque
            return dryRing_[c][(base + static_cast<uint32_t>(n)) & kRingMask];
        };
        const float ym1 = tap(n0 - 1), y0 = tap(n0), y1 = tap(n0 + 1), y2 = tap(n0 + 2);
        const float c0 = y0;
        const float c1 = y1 - ym1 / 3.f - y0 / 2.f - y2 / 6.f;
        const float c2 = 0.5f * (ym1 + y1) - y0;
        const float c3 = (y2 - ym1) / 6.f + 0.5f * (y0 - y1);
        return ((c3 * fr + c2) * fr + c1) * fr + c0;
    }

    void advanceDelay() {
        if (xf_ > 0) return;                         // congelado durante el crossfade
        const double err = dTarget_ - dCur_;
        if (std::fabs(err) > 1.0) {                  // salto: crossfade entre posiciones
            dOld_ = dCur_; dCur_ = dTarget_; xf_ = kXfSamples;
        } else {                                     // corrección gradual
            dCur_ += std::clamp(err, -static_cast<double>(kSlewPerSmp), static_cast<double>(kSlewPerSmp));
        }
    }

    void estimateDspLag(const float* wet, int frames, uint32_t base) {
        if (++lagCounter_ < kLagEvery) return;
        lagCounter_ = 0;
        if (frames < 128) return;
        const int K = kMaxDspLag;
        // mono seco con K muestras de historial
        for (int n = 0; n < frames + K; ++n) {
            const uint32_t idx = (base + static_cast<uint32_t>(n) - static_cast<uint32_t>(K)) & kRingMask;
            lagDry_[n] = 0.5f * (dryRing_[0][idx] + dryRing_[1][idx]);
        }
        double ew = 0;
        for (int i = 0; i < frames; ++i) {
            lagWet_[i] = 0.5f * (fin(wet[2 * i]) + fin(wet[2 * i + 1]));
            ew += static_cast<double>(lagWet_[i]) * lagWet_[i];
        }
        if (ew < 1e-9) return;                       // silencio: sin información
        double ed = 0;
        for (int i = 0; i < frames; ++i) ed += static_cast<double>(lagDry_[K + i]) * lagDry_[K + i];
        float best = -2.f; int bestT = 0;
        for (int t = 0; t <= K; ++t) {
            double c = 0;
            const float* dd = &lagDry_[K - t];
            for (int i = 0; i < frames; ++i) c += static_cast<double>(lagWet_[i]) * dd[i];
            const float nrm = static_cast<float>(c / std::sqrt(ew * ed + 1e-18));
            lagCorr_[t] = nrm;
            if (nrm > best + 0.02f) { best = nrm; bestT = t; }     // empate → menor retardo
            // ventana deslizante de energía seca para t+1
            if (t < K) {
                const double a = lagDry_[K - 1 - t], b = lagDry_[K + frames - 1 - t];
                ed += a * a - b * b;
                if (ed < 0) ed = 0;
            }
        }
        // preferir 0 salvo mejora clara
        if (bestT != 0 && lagCorr_[bestT] < lagCorr_[0] + 0.05f) { bestT = 0; best = lagCorr_[0]; }
        float tau = static_cast<float>(bestT);
        if (bestT > 0 && bestT < K) {                // interpolación parabólica del pico
            const float ym = lagCorr_[bestT - 1], y0 = lagCorr_[bestT], yp = lagCorr_[bestT + 1];
            const float den = ym - 2.f * y0 + yp;
            if (den < -1e-9f) tau += std::clamp(0.5f * (ym - yp) / den, -0.5f, 0.5f);
        }
        lagConf_ = std::clamp(best, 0.f, 1.f);
        tConf_.store(lagConf_, std::memory_order_relaxed);
        if (lagConf_ >= 0.5f) {
            dTarget_ += 0.25 * (static_cast<double>(tau) - dTarget_);
            if (dTarget_ < 0.0) dTarget_ = 0.0;
        }
        tDspLag_.store(static_cast<float>(dCur_), std::memory_order_relaxed);
    }

    // FFT radix-2 in situ sobre fftRe_/fftIm_ (tablas precalculadas; sin asignaciones).
    void fftInPlace() {
        float* re = fftRe_.data(); float* im = fftIm_.data();
        for (int i = 0; i < kFft; ++i) {
            const int j = rev_[i];
            if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int len = 2; len <= kFft; len <<= 1) {
            const int half = len >> 1, step = kFft / len;
            for (int i = 0; i < kFft; i += len) {
                for (int k = 0, t = 0; k < half; ++k, t += step) {
                    const float wr = cosT_[t], wi = sinT_[t];
                    const int u = i + k, v = u + half;
                    const float vr = re[v] * wr - im[v] * wi, vi = re[v] * wi + im[v] * wr;
                    re[v] = re[u] - vr; im[v] = im[u] - vi;
                    re[u] += vr;        im[u] += vi;
                }
            }
        }
    }

    // Espectro cruzado delta·x* (emparejando x(t) con delta(t−L)) y ganancia coherente por banda.
    void analyzeCoherence(int Ls) {
        const uint32_t end = pos_;
        for (int n = 0; n < kFft; ++n) {
            const uint32_t idx = end - static_cast<uint32_t>(kFft) + static_cast<uint32_t>(n);
            fftRe_[n] = win_[n] * xMono_[idx & kRingMask];
            fftIm_[n] = win_[n] * dMono_[(idx - static_cast<uint32_t>(Ls)) & kRingMask];
        }
        fftInPlace();
        const float alpha = 0.30f;                                  // ≈ 5.7 promedios efectivos (≈ 0.25 s)
        const float* re = fftRe_.data(); const float* im = fftIm_.data();
        for (int k = 1; k < kFft / 2; ++k) {
            const float ar = re[k], ai = im[k], br = re[kFft - k], bi = -im[kFft - k];   // Z(k), conj Z(N−k)
            const float xr = 0.5f * (ar + br), xi = 0.5f * (ai + bi);                    // X(k)
            const float dr = 0.5f * (ai - bi), di = -0.5f * (ar - br);                   // Δ(k)
            sxx_[k]  += alpha * (xr * xr + xi * xi - sxx_[k]);
            sdd_[k]  += alpha * (dr * dr + di * di - sdd_[k]);
            sdxR_[k] += alpha * (dr * xr + di * xi - sdxR_[k]);                          // Re(Δ·X*)
            sdxI_[k] += alpha * (di * xr - dr * xi - sdxI_[k]);                          // Im(Δ·X*)
        }
        if (analyses_ < 1000000) ++analyses_;

        float pw[kBands] = {0, 0, 0}, eX[kBands] = {0, 0, 0}, eD[kBands] = {0, 0, 0};
        int   nb[kBands] = {0, 0, 0};
        const float M = 5.f;                                        // promedios efectivos (sesgo de γ²)
        // solape de ventanas entre x(t) y delta(t−L): ξ = Σ w(n)·w(n+L) / Σ w²
        double xiNum = 0, xiDen = 0;
        for (int n = 0; n < kFft; ++n) {
            xiDen += static_cast<double>(win_[n]) * win_[n];
            if (n + Ls < kFft) xiNum += static_cast<double>(win_[n]) * win_[n + Ls];
        }
        const float xi = static_cast<float>(xiNum / (xiDen + 1e-30));
        const bool  resolvable = xi >= 0.3f;
        for (int k = 1; k < kFft / 2; ++k) {
            const int b = bandOf_[k];
            ++nb[b];
            eX[b] += sxx_[k]; eD[b] += sdd_[k];
            if (sxx_[k] < 1e-10f || sdd_[k] < 1e-14f) continue;     // silencio: sin información
            const float cross = sdxR_[k] * sdxR_[k] + sdxI_[k] * sdxI_[k];
            float g2 = cross / (sxx_[k] * sdd_[k] + 1e-30f);
            g2 = std::clamp((g2 * M - 1.f) / (M - 1.f), 0.f, 1.f);  // corrección de sesgo 1/M
            g2 = resolvable ? std::clamp(g2 / (xi * xi), 0.f, 1.f) : 1.f;   // solape de ventanas / peor caso
            pw[b] += g2 * sdd_[k] / sxx_[k];                         // |H|² coherente
        }
        for (int b = 0; b < kBands; ++b) {
            const float rms = nb[b] > 0 ? std::sqrt(pw[b] / static_cast<float>(nb[b])) : 0.f;
            coh_[b]   = 1.6f * rms;                                  // factor de cresta (Rayleigh)
            ratio_[b] = eX[b] > 1e-12f ? std::sqrt(eD[b] / eX[b]) : 0.f;
        }
    }

    void updateGains() {
        const float lamBlock = blockSeconds_;
        const float aAtk = 1.f - std::exp(-lamBlock / 0.020f);   // protección: rápida (20 ms)
        const float aRel = 1.f - std::exp(-lamBlock / 0.400f);   // recuperación: lenta (400 ms)
        const bool  warm = analyses_ >= kWarmHops;

        float coh[kBands], ratio[kBands];
        for (int b = 0; b < kBands; ++b) {
            coh[b]   = warm ? coh_[b] : 1.f;                      // sin datos: peor caso (fidelidad primero)
            ratio[b] = ratio_[b];
            tCorr_[b].store(coh[b], std::memory_order_relaxed);
            tRatio_[b].store(ratio[b], std::memory_order_relaxed);
        }

        // ── Inmersión perceptual: techo = f(coherencia, fase, energía, latencia, contenido) ──
        const float Lms = latencyMs_.load(std::memory_order_relaxed);
        const float latF   = 1.f - 0.6f * smoothstep(12.f, 40.f, Lms);            // Haas: ≤12 ms fusiona
        const float cMax   = std::max(coh[1], coh[2]);
        const float corrF  = 1.f - 0.7f * smoothstep(0.10f, 0.60f, cMax);
        const float phaseF = 0.5f + 0.5f * std::sqrt(std::clamp(lagConf_ > 0.f ? lagConf_ : 1.f, 0.f, 1.f));
        const float rTot   = std::max(ratio[1], ratio[2]);
        const float over   = std::max(0.f, rTot - 0.7f);
        const float energF = 1.f / (1.f + 3.f * over * over);
        const float sp = speech_.load(std::memory_order_relaxed), tn = tonal_.load(std::memory_order_relaxed);
        const float contF  = (1.f - 0.5f * sp) * (1.f - 0.15f * tn);
        const float ceiling = std::clamp(latF * corrF * phaseF * energF * contF, 0.f, 1.f);
        ceilNext_ = ceiling;
        const float user = intensity_.load(std::memory_order_relaxed);

        float risk = 0.f;
        for (int b = 0; b < kBands; ++b) {
            float g = user * cap_[b].load(std::memory_order_relaxed) * ceiling;
            if (coh[b] > 1e-6f) g = std::min(g, kRiskMax / coh[b]);       // guardián: g·c ≤ 10^(R/20) − 1
            const float a = (g < gNext_[b]) ? aAtk : aRel;
            gNext_[b] = std::clamp(gNext_[b] + a * (g - gNext_[b]), 0.f, 4.f);
            risk = std::max(risk, gNext_[b] * coh[b]);
            tGain_[b].store(gNext_[b], std::memory_order_relaxed);
        }
        tRisk_.store(risk, std::memory_order_relaxed);
        tCeil_.store(ceiling, std::memory_order_relaxed);
        tImm_.store(user * ceiling, std::memory_order_relaxed);
    }

    // ── estado ──────────────────────────────────────────────────────────────
    bool  prepared_ = false;
    float sr_ = 48000.f, blockSeconds_ = 0.f;
    int   maxFrames_ = 1024;
    uint32_t pos_ = 0;
    int   lagCounter_ = 0;
    double dCur_ = 0.0, dTarget_ = 0.0, dOld_ = 0.0;
    int   xf_ = 0;
    float lagConf_ = 0.f;

    std::vector<float> mem_, lagWet_, lagDry_, lagCorr_;
    float* dryRing_[2] = {nullptr, nullptr};
    float* xMono_ = nullptr; float* dMono_ = nullptr;
    std::vector<float> fftRe_, fftIm_, win_, cosT_, sinT_, sxx_, sdd_, sdxR_, sdxI_;
    std::vector<int>   rev_;
    std::vector<uint8_t> bandOf_;
    int    hopCount_ = 0, analyses_ = 0;
    float  coh_[kBands] = {1.f, 1.f, 1.f}, ratio_[kBands] = {0, 0, 0};
    Chain  fDelta_[2];        // un banco de filtros por canal
    Chain  fDry_[2];

    float  gCur_[kBands] = {0, 0, 0}, gNext_[kBands] = {0, 0, 0};
    float  ceilCur_ = 0.f, ceilNext_ = 0.f;
    float  transFast_[2] = {0.f, 0.f}, transSlow_[2] = {0.f, 0.f};

    std::atomic<float> intensity_{0.6f}, latencyMs_{30.f}, speech_{0.f}, tonal_{0.f};
    std::atomic<float> cap_[kBands] = {{0.25f}, {0.70f}, {1.0f}};
    std::atomic<float> tDspLag_{0.f}, tConf_{0.f}, tCorr_[kBands] = {{0.f}, {0.f}, {0.f}},
                       tRatio_[kBands] = {{0.f}, {0.f}, {0.f}}, tGain_[kBands] = {{0.f}, {0.f}, {0.f}},
                       tRisk_{0.f}, tImm_{0.f}, tCeil_{0.f};
};

} // namespace ivanna
