// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// PredictiveAudioEngine — ver predictive_audio_engine.hpp para el contrato
// completo (qué es, límites físicos, validación, restricciones de tiempo real).

#include "predictive_audio_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace ivanna {

namespace {

constexpr int    kMask          = PredictiveAudioEngine::kWindow - 1;
constexpr double kResidualFloor = 16.0;   // potencia mínima del residuo (≈ 4 LSB rms): evita ratios absurdos en silencio
constexpr double kSilencePower  = 1.0;    // potencia media < 1 LSB² → sin modelo
constexpr double kMinMeanPower  = 4.0;    // potencia media mínima para fiarse de una medición de NMSE
constexpr double kNmseAlpha     = 0.3;    // suavizado por hop de las NMSE
constexpr int    kDataFrac      = 8;      // bits fraccionarios de los datos en la recursión entera (Q8)
constexpr int    kMaxCoefShift  = 28;     // coef int32: |c|·2^s < 2^31

inline int clampi(int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline int16_t toQ15(float x) noexcept {
    if (!std::isfinite(x)) return 0;               // NaN/Inf no envenenan el estado
    float v = x * 32768.0f;
    if (v > 32767.0f)  v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    return static_cast<int16_t>(std::lrintf(v));
}

inline int16_t satQ15(int64_t v) noexcept {
    return static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

} // namespace

// ───────────────────────────────────────────────────────────────────────────
void PredictiveAudioEngine::prepare(float sampleRate, const PredictiveConfig& cfg) noexcept {
    sr_  = (std::isfinite(sampleRate) && sampleRate >= 8000.f) ? sampleRate : 48000.f;
    cfg_ = cfg;
    cfg_.bandwidthGamma = std::isfinite(cfg.bandwidthGamma)
                              ? std::min(1.0f, std::max(0.9f, cfg.bandwidthGamma)) : 0.9998f;
    cfg_.onsetRatio     = (std::isfinite(cfg.onsetRatio) && cfg.onsetRatio > 1.f) ? cfg.onsetRatio : 12.f;
    cfg_.minConfidence  = std::isfinite(cfg.minConfidence)
                              ? std::min(1.0f, std::max(0.0f, cfg.minConfidence)) : 0.5f;

    order_    = clampi(cfg.order, 4, kMaxOrder);
    horizon_  = clampi(cfg.horizon, 16, kMaxHorizon);
    analysis_ = std::max(clampi(cfg.analysisLen, 64, kWindow), 4 * order_);
    analysis_ = std::min(analysis_, kWindow);
    // Con `hop` < H/kSlots habría más pronósticos simultáneos que slots.
    const int minHop = (horizon_ + kSlots - 1) / kSlots;
    hop_      = clampi(cfg.hop, minHop, horizon_);

    alphaFast_  = 1.0 - std::exp(-1.0 / (0.001 * sr_));   // τ = 1 ms
    alphaSlow_  = 1.0 - std::exp(-1.0 / (0.100 * sr_));   // τ = 100 ms
    refractory_ = static_cast<int>(0.020 * sr_);          // 20 ms
    reset();
}

void PredictiveAudioEngine::reset() noexcept {
    ring_.fill(0);
    wpos_ = 0; total_ = 0; sinceHop_ = 0;
    haveModel_ = false; modelOrder_ = 0; qshift_ = 1;
    q_.fill(0); c_.fill(0.0);
    for (auto& s : slots_) { s.fc.fill(0); s.start = 0; s.active = false; }
    slotNext_ = 0;
    errNear_ = sigNear_ = errFar_ = sigFar_ = 0.0;
    cntNear_ = cntFar_ = 0;
    nmseNear_ = nmseFar_ = 1.0;
    envFast_ = envSlow_ = 0.0;
    sinceOnset_ = 1 << 30; onsetsThisHop_ = 0; onsetCount_ = 0; lastOnset_ = -1;
    frame_ = PredictiveFrame{};
    forecastOut_.fill(0.f);
}

// ───────────────────────────────────────────────────────────────────────────
void PredictiveAudioEngine::processMono(const float* x, int n) noexcept {
    if (!x || n <= 0) return;
    for (int i = 0; i < n; ++i) pushSample(x[i]);
}

void PredictiveAudioEngine::processStereo(const float* l, const float* r, int n) noexcept {
    if (!l || !r || n <= 0) return;
    for (int i = 0; i < n; ++i) {
        const float a = std::isfinite(l[i]) ? l[i] : 0.f;
        const float b = std::isfinite(r[i]) ? r[i] : 0.f;
        pushSample(0.5f * (a + b));
    }
}

// ───────────────────────────────────────────────────────────────────────────
int PredictiveAudioEngine::predictOne() const noexcept {
    const int p = modelOrder_;
    if (cfg_.quantized) {
        int64_t acc = 0;
        for (int k = 1; k <= p; ++k)
            acc += static_cast<int64_t>(q_[static_cast<size_t>(k - 1)]) * ring_[(wpos_ - k) & kMask];
        return satQ15((acc + (int64_t(1) << (qshift_ - 1))) >> qshift_);
    }
    double acc = 0.0;
    for (int k = 1; k <= p; ++k) acc += c_[k - 1] * ring_[(wpos_ - k) & kMask];
    return satQ15(static_cast<int64_t>(std::lrint(acc)));
}

void PredictiveAudioEngine::pushSample(float xf) noexcept {
    const int16_t x = toQ15(xf);

    // 1) Detector de onset por residuo: e[n] = x[n] − x̂[n|n−1].
    ++sinceOnset_;
    if (haveModel_) {
        const double e  = static_cast<double>(x) - static_cast<double>(predictOne());
        const double e2 = e * e;
        envFast_ += (e2 - envFast_) * alphaFast_;
        envSlow_ += (e2 - envSlow_) * alphaSlow_;
        if (sinceOnset_ >= refractory_ &&
            envFast_ > static_cast<double>(cfg_.onsetRatio) * (envSlow_ + kResidualFloor)) {
            sinceOnset_ = 0;
            ++onsetsThisHop_;
            ++onsetCount_;
            lastOnset_ = static_cast<int64_t>(total_);
        }
    }

    // 2) Validación en lazo cerrado de los pronósticos vivos.
    for (auto& s : slots_) {
        if (!s.active) continue;
        const uint64_t age = total_ - s.start;
        if (age >= static_cast<uint64_t>(horizon_)) { s.active = false; continue; }
        const double err = static_cast<double>(x) - static_cast<double>(s.fc[static_cast<size_t>(age)]);
        const double sg  = static_cast<double>(x) * static_cast<double>(x);
        if (static_cast<int>(age) < horizon_ / 2) { errNear_ += err * err; sigNear_ += sg; ++cntNear_; }
        else                                      { errFar_  += err * err; sigFar_  += sg; ++cntFar_;  }
    }

    // 3) Historia.
    ring_[static_cast<size_t>(wpos_)] = x;
    wpos_ = (wpos_ + 1) & kMask;
    ++total_;

    // 4) Re-ajuste del modelo cada `hop` muestras.
    if (++sinceHop_ >= hop_ && total_ >= static_cast<uint64_t>(analysis_)) {
        sinceHop_ = 0;
        updateModel();
    }
}

// ───────────────────────────────────────────────────────────────────────────
void PredictiveAudioEngine::clearModel() noexcept {
    haveModel_ = false;
    modelOrder_ = 0;
    forecastOut_.fill(0.f);
    frame_.valid = false;
    frame_.confidence = 0.f;
    frame_.predictedPeak = 0.f;
    frame_.predictedSlewPerMs = 0.f;
    frame_.zeroCrossHz = 0.f;
    frame_.predictionGainDb = 0.f;
}

void PredictiveAudioEngine::storeForecast(const int16_t* fc) noexcept {
    Slot& s = slots_[static_cast<size_t>(slotNext_)];
    for (int i = 0; i < horizon_; ++i) s.fc[static_cast<size_t>(i)] = fc[i];
    s.start  = total_;      // fc[0] es la muestra de índice `total_` (aún no llegada)
    s.active = true;
    slotNext_ = (slotNext_ + 1) % kSlots;
    for (int i = 0; i < horizon_; ++i) forecastOut_[static_cast<size_t>(i)] = static_cast<float>(fc[i]) * (1.0f / 32768.0f);
}

void PredictiveAudioEngine::updateModel() noexcept {
    // ── 1. Cierra la ventana de validación y actualiza NMSE ─────────────
    if (cntFar_ > 0 && sigFar_ >= kMinMeanPower * cntFar_) {
        const double m = std::min(4.0, errFar_ / sigFar_);
        nmseFar_ = (1.0 - kNmseAlpha) * nmseFar_ + kNmseAlpha * m;
    }
    if (cntNear_ > 0 && sigNear_ >= kMinMeanPower * cntNear_) {
        const double m = std::min(4.0, errNear_ / sigNear_);
        nmseNear_ = (1.0 - kNmseAlpha) * nmseNear_ + kNmseAlpha * m;
    }
    errNear_ = sigNear_ = errFar_ = sigFar_ = 0.0;
    cntNear_ = cntFar_ = 0;

    // ── 2. Método de Burg sobre las últimas `analysis_` muestras ────────
    const int N = analysis_;
    double energy = 0.0;
    int peak = 0;
    for (int i = 0; i < N; ++i) {
        const int16_t s = ring_[static_cast<size_t>((wpos_ - N + i) & kMask)];
        f_[static_cast<size_t>(i)] = b_[static_cast<size_t>(i)] = static_cast<double>(s);
        energy += static_cast<double>(s) * s;
        peak = std::max(peak, std::abs(static_cast<int>(s)));
    }
    const double meanPower = energy / N;

    bool modelOk = false;
    if (meanPower >= kSilencePower) {
        double a[kMaxOrder + 1] = {1.0};
        double tmp[kMaxOrder + 1];
        double E = meanPower;
        int p = 0;
        for (int m = 1; m <= order_; ++m) {
            double num = 0.0, den = 0.0;
            for (int n = m; n < N; ++n) {
                const double fn = f_[static_cast<size_t>(n)], bn = b_[static_cast<size_t>(n - 1)];
                num += fn * bn;
                den += fn * fn + bn * bn;
            }
            if (!(den > 1e-9)) break;
            const double k = -2.0 * num / den;       // |k| ≤ 1 por construcción (Burg ⇒ polos estables)
            for (int i = 1; i < m; ++i) tmp[i] = a[i] + k * a[m - i];
            for (int i = 1; i < m; ++i) a[i] = tmp[i];
            a[m] = k;
            for (int n = N - 1; n >= m; --n) {       // descendente: actualización in situ de b[n-1]
                const double fn = f_[static_cast<size_t>(n)], bn1 = b_[static_cast<size_t>(n - 1)];
                f_[static_cast<size_t>(n)] = fn + k * bn1;
                b_[static_cast<size_t>(n)] = bn1 + k * fn;
            }
            E *= (1.0 - k * k);
            p = m;
        }

        if (p >= 1 && std::isfinite(E) && E > 0.0) {
            // Predictor: x̂[n] = −Σ a_k·γ^k · x[n−k]
            double maxAbs = 0.0, g = 1.0;
            for (int k = 1; k <= p; ++k) {
                g *= static_cast<double>(cfg_.bandwidthGamma);
                c_[static_cast<size_t>(k - 1)] = -a[k] * g;
                maxAbs = std::max(maxAbs, std::fabs(c_[static_cast<size_t>(k - 1)]));
            }
            for (int k = p; k < kMaxOrder; ++k) c_[static_cast<size_t>(k)] = 0.0;

            // Formato Q dinámico: el mayor shift que mantiene |coef| en int32.
            int s = kMaxCoefShift;
            while (s > 1 && maxAbs * static_cast<double>(int64_t(1) << s) > 2147483647.0) --s;
            qshift_ = s;
            const double scale = static_cast<double>(int64_t(1) << s);
            for (int k = 0; k < kMaxOrder; ++k) {
                const double v = std::nearbyint(c_[static_cast<size_t>(k)] * scale);
                q_[static_cast<size_t>(k)] = static_cast<int32_t>(std::min(2147483647.0, std::max(-2147483647.0, v)));
            }
            modelOrder_ = p;
            haveModel_  = true;
            frame_.predictionGainDb = static_cast<float>(10.0 * std::log10(meanPower / E));

            // ── 3. Pronóstico de `horizon_` muestras ────────────────────
            int32_t  ibuf[kMaxOrder + kMaxHorizon];     // Q8: muestra · 256
            double   dbuf[kMaxOrder + kMaxHorizon];
            int16_t  fc[kMaxHorizon];
            for (int i = 0; i < p; ++i) {
                const int16_t v = ring_[static_cast<size_t>((wpos_ - p + i) & kMask)];
                ibuf[i] = static_cast<int32_t>(v) * (1 << kDataFrac);
                dbuf[i] = static_cast<double>(v);
            }
            constexpr int64_t kQ8Max = int64_t(32767) << kDataFrac;
            int fcPeak = 0;
            for (int i = 0; i < horizon_; ++i) {
                int16_t out;
                if (cfg_.quantized) {
                    int64_t acc = 0;
                    for (int k = 1; k <= p; ++k)
                        acc += static_cast<int64_t>(q_[static_cast<size_t>(k - 1)]) * ibuf[p + i - k];
                    int64_t v = (acc + (int64_t(1) << (qshift_ - 1))) >> qshift_;      // Q8
                    v = v > kQ8Max ? kQ8Max : (v < -kQ8Max ? -kQ8Max : v);
                    ibuf[p + i] = static_cast<int32_t>(v);
                    out = satQ15((v + (int64_t(1) << (kDataFrac - 1))) >> kDataFrac);
                } else {
                    double acc = 0.0;
                    for (int k = 1; k <= p; ++k) acc += c_[static_cast<size_t>(k - 1)] * dbuf[p + i - k];
                    if (!std::isfinite(acc)) acc = 0.0;
                    acc = std::min(32767.0, std::max(-32768.0, acc));
                    dbuf[p + i] = acc;
                    out = satQ15(static_cast<int64_t>(std::lrint(acc)));
                }
                fc[i] = out;
                fcPeak = std::max(fcPeak, std::abs(static_cast<int>(out)));
            }

            // Guarda de inestabilidad: un pronóstico que crece mucho más que
            // la propia señal observada no es una extrapolación, es divergencia.
            if (fcPeak <= 2 * peak + 256) {
                storeForecast(fc);
                ++frame_.forecastsGenerated;
                modelOk = true;

                int maxStep = 0, nc = 0;
                double firstPos = 0.0, lastPos = 0.0;
                for (int i = 1; i < horizon_; ++i) {
                    const int a0 = fc[i - 1], b0 = fc[i];
                    maxStep = std::max(maxStep, std::abs(b0 - a0));
                    if ((a0 < 0 && b0 >= 0) || (a0 >= 0 && b0 < 0)) {
                        const double pos = (i - 1) + static_cast<double>(std::abs(a0)) / (std::abs(a0) + std::abs(b0));
                        if (nc == 0) firstPos = pos;
                        lastPos = pos;
                        ++nc;
                    }
                }
                frame_.predictedPeak      = static_cast<float>(fcPeak) * (1.0f / 32768.0f);
                frame_.predictedSlewPerMs = static_cast<float>(maxStep) * (1.0f / 32768.0f) * (sr_ * 0.001f);
                frame_.zeroCrossHz = (nc >= 3 && lastPos > firstPos)
                    ? static_cast<float>(0.5 * (nc - 1) * static_cast<double>(sr_) / (lastPos - firstPos)) : 0.f;
            } else {
                ++frame_.forecastsRejected;
            }
        } else {
            ++frame_.forecastsRejected;
        }
    } else {
        ++frame_.forecastsRejected;     // silencio: no hay nada que pronosticar
    }

    if (!modelOk) clearModel();

    // ── 4. Frame de salida ──────────────────────────────────────────────
    frame_.nmseNear   = static_cast<float>(nmseNear_);
    frame_.nmseFar    = static_cast<float>(nmseFar_);
    const float conf  = static_cast<float>(std::min(1.0, std::max(0.0, 1.0 - nmseFar_)));
    frame_.confidence = modelOk ? conf : 0.f;
    frame_.valid      = modelOk && conf >= cfg_.minConfidence;
    frame_.transient  = onsetsThisHop_ > 0;
    onsetsThisHop_    = 0;
    frame_.onsetCount = onsetCount_;
    frame_.lastOnsetSample = lastOnset_;
}

} // namespace ivanna
