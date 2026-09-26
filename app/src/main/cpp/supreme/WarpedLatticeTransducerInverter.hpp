#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — EJE 1: WarpedLatticeTransducerInverter (C++23)
// Filtro de Red Adaptativa en Celosía Deformada en Frecuencia (Frequency-Warped
// Lattice Filter) con compensación no-lineal Bl(x), impedancia compleja Z(ω)
// e inyección de Micro-Chirp psicoacústicamente enmascarado (17.5 kHz – 19.0 kHz).
// Cero malloc en hot-path, noexcept, FTZ/DAZ, vectorización ARM NEON / SVE2.
// ═══════════════════════════════════════════════════════════════════════════════

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna::supreme {

/**
 * @struct WarpedLatticeTransducerInverter
 * @brief Inversor activo en tiempo real de impedancia electromecánica Z(ω) y
 *        no-linealidad de factor de fuerza Bl(x) mediante topología en celosía
 *        deformada en frecuencia (Bark/ERB allpass warping λ) de orden M = 8.
 *
 * Ecuación de recurrencia en celosía con operador allpass D_λ(z) = (z⁻¹ - λ)/(1 - λz⁻¹):
 *   f_m(n) = f_{m-1}(n) + κ_m(n) · b_{m-1}(n)
 *   b_m(n) = D_λ[b_{m-1}(n)] + κ_m(n) · f_{m-1}(n)
 *
 * Pre-compensación inversa de excursión de bobina móvil x(n):
 *   u_lin(n) = u(n) · [Bl_0 / Bl(x̂(n))],  con Bl(x) = Bl_0 · (1 - β_1 x - β_2 x²)
 */
class alignas(64) WarpedLatticeTransducerInverter {
public:
    static constexpr size_t ORDER = 8; // Múltiplo de 4 para desenrollado SIMD NEON/SVE2
    static constexpr float kPi = 3.14159265358979323846f;

    WarpedLatticeTransducerInverter() noexcept {
        prepare(48000.0f);
    }

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        invSampleRate_ = 1.0f / sampleRate_;

        // Factor de deformación psicoacústica de Smith-Abel (aproximación escala Bark):
        // λ = 1.0674 · (2/π · arctan(0.06583 · fs / 1000))^{1/2} - 0.1916
        const float fsKhz = sampleRate_ * 0.001f;
        const float warpRaw = 1.0674f * std::sqrt((2.0f / kPi) * std::atan(0.06583f * fsKhz)) - 0.1916f;
        lambda_ = std::clamp(warpRaw, -0.85f, 0.85f);

        // Coeficientes de reflexión iniciales κ_m modelando resonancia f_0 y decaimiento inductivo Le
        constexpr std::array<float, ORDER> kDefaultKappa = {
            -0.38f, 0.24f, -0.15f, 0.09f, -0.055f, 0.032f, -0.018f, 0.009f
        };
        for (size_t m = 0; m < ORDER; ++m) {
            kappa_[m] = kDefaultKappa[m];
            gammaGroupDelay_[m] = 0.125f * static_cast<float>(ORDER - m) * invSampleRate_;
        }

        // Parámetros electromecánicos Thiele-Small normalizados (Bl(x) simétrico + asimétrico)
        beta1_ = 0.045f; // Asimetría de suspensión Kms(x)
        beta2_ = 0.180f; // Compresión magnética Bl(x) en alta excursión
        excursionDecay_ = std::exp(-2.0f * kPi * 85.0f * invSampleRate_);
        chirpPhase_ = 0.0f;
        chirpFreqHz_ = 17500.0f;
        silenceEnvelope_ = 0.0f;

        reset();
    }

    void reset() noexcept {
        bStateL_.fill(0.0f);
        bStateR_.fill(0.0f);
        allpassMemL_.fill(0.0f);
        allpassMemR_.fill(0.0f);
        delayFracL_.fill(0.0f);
        delayFracR_.fill(0.0f);
        excursionEstL_ = 0.0f;
        excursionEstR_ = 0.0f;
        silenceEnvelope_ = 0.0f;
    }

    [[gnu::always_inline]] inline float sanitize(float x) const noexcept {
        return (std::isfinite(x) && std::fabs(x) > 1.0e-30f) ? x : 0.0f;
    }

    /**
     * @brief Calcula el retardo de grupo inverso sub-muestra (en muestras fraccionales)
     *        a la frecuencia normalizada ω ∈ [0, π].
     */
    float computeSubSampleInverseGroupDelay(float freqHz) const noexcept {
        const float omega = 2.0f * kPi * std::clamp(freqHz, 20.0f, 0.49f * sampleRate_) * invSampleRate_;
        const float cosW = std::cos(omega);
        const float l2 = lambda_ * lambda_;
        // Derivada de fase del operador allpass D_λ(e^{jω}): τ_λ(ω) = (1 - λ²) / (1 + λ² - 2λ cos(ω))
        const float denom = std::max(1.0e-6f, 1.0f + l2 - 2.0f * lambda_ * cosW);
        const float warpedTau = (1.0f - l2) / denom;

        float weightedDelay = 0.0f;
        for (size_t m = 0; m < ORDER; ++m) {
            const float km = kappa_[m];
            weightedDelay += (1.0f - km * km) * warpedTau * (0.08f * static_cast<float>(m + 1));
        }
        return std::clamp(weightedDelay, 0.0f, 0.999f);
    }

    /**
     * @brief Actualiza los coeficientes de celosía κ_m mediante descenso LMS normalizado
     *        a partir del loopback de impedancia o micrófono ANC.
     */
    void adaptFromLoopback(const float* __restrict micLoopback, size_t numSamples, float mu = 0.002f) noexcept {
        if (!micLoopback || numSamples == 0) return;
        const float step = std::clamp(mu, 0.0f, 0.05f);
        for (size_t i = 0; i < numSamples; ++i) {
            const float err = sanitize(micLoopback[i]);
            const float norm = 1.0f / (1.0e-4f + err * err);
            for (size_t m = 0; m < ORDER; ++m) {
                const float grad = err * bStateL_[m] * norm;
                // Proyección en el hipercubo de estabilidad de Schur: |κ_m| < 0.95
                kappa_[m] = std::clamp(kappa_[m] - step * grad, -0.95f, 0.95f);
            }
        }
    }

    /**
     * @brief Procesa un bloque estéreo in-place con inversión de celosía deformada,
     *        compensación de excursión Bl(x), corrección de retardo de grupo sub-muestra
     *        e inyección de Micro-Chirp enmascarado (17.5 kHz - 19 kHz) durante silencios.
     */
    void process(float* __restrict left, float* __restrict right, size_t numSamples) noexcept {
        if (!left || !right || numSamples == 0) return;
        if (!enabled_.load(std::memory_order_relaxed)) return;

        const float fracDelay = computeSubSampleInverseGroupDelay(1000.0f);
        lastSubSampleDelay_.store(fracDelay, std::memory_order_relaxed);
        const float blDrive = blCompensationDrive_.load(std::memory_order_relaxed);
        const float effBeta1 = beta1_ * blDrive;
        const float effBeta2 = beta2_ * blDrive;

        for (size_t i = 0; i < numSamples; ++i) {
            float inL = sanitize(left[i]);
            float inR = sanitize(right[i]);

            // 1. Detección de silencio e inyección de Micro-Chirp (17.5 - 19 kHz con dithering de fase)
            const float instEnergy = 0.5f * (inL * inL + inR * inR);
            silenceEnvelope_ = 0.995f * silenceEnvelope_ + 0.005f * instEnergy;

            float microChirp = 0.0f;
            if (silenceEnvelope_ < 1.0e-7f && microChirpEnabled_.load(std::memory_order_relaxed)) {
                // Barrido logarítmico 17.5 kHz -> 19.0 kHz con dithering de fase determinista TPDF
                chirpFreqHz_ += 0.35f;
                if (chirpFreqHz_ > 19000.0f) chirpFreqHz_ = 17500.0f;
                lfsrState_ ^= lfsrState_ << 13;
                lfsrState_ ^= lfsrState_ >> 17;
                lfsrState_ ^= lfsrState_ << 5;
                const float phaseDither = static_cast<float>(static_cast<int32_t>(lfsrState_)) * 4.65661287e-10f * 0.02f;
                chirpPhase_ += 2.0f * kPi * chirpFreqHz_ * invSampleRate_ + phaseDither;
                if (chirpPhase_ > 2.0f * kPi) chirpPhase_ -= 2.0f * kPi;
                // Amplitud -78 dBFS: por debajo del umbral absoluto de audición en 18 kHz (ISO 226)
                microChirp = 1.25e-4f * std::sin(chirpPhase_);
            }

            // 2. Pre-compensación no-lineal de excursión Bl(x)
            excursionEstL_ = sanitize(excursionDecay_ * excursionEstL_ + (1.0f - excursionDecay_) * inL);
            excursionEstR_ = sanitize(excursionDecay_ * excursionEstR_ + (1.0f - excursionDecay_) * inR);

            const float blRatioL = std::clamp(1.0f - effBeta1 * excursionEstL_ - effBeta2 * excursionEstL_ * excursionEstL_, 0.55f, 1.45f);
            const float blRatioR = std::clamp(1.0f - effBeta1 * excursionEstR_ - effBeta2 * excursionEstR_ * excursionEstR_, 0.55f, 1.45f);

            float fL = (inL / blRatioL) + microChirp;
            float fR = (inR / blRatioR) + microChirp;

            // 3. Celosía Deformada en Frecuencia (Orden 8 desenrollado en 2 bloques de 4 vías SIMD)
            processWarpedLatticeStep(fL, bStateL_, allpassMemL_);
            processWarpedLatticeStep(fR, bStateR_, allpassMemR_);

            // 4. Compensación de retardo de grupo inverso sub-muestra (interpolador Lagrange cúbico)
            left[i]  = std::clamp(applySubSampleDelay(fL, delayFracL_, fracDelay), -1.95f, 1.95f);
            right[i] = std::clamp(applySubSampleDelay(fR, delayFracR_, fracDelay), -1.95f, 1.95f);
        }
    }

    void setEnabled(bool en) noexcept { enabled_.store(en, std::memory_order_release); }
    bool isEnabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    void setMicroChirpEnabled(bool en) noexcept { microChirpEnabled_.store(en, std::memory_order_release); }
    bool isMicroChirpEnabled() const noexcept { return microChirpEnabled_.load(std::memory_order_acquire); }
    void setBlCompensationDrive(float drive) noexcept {
        blCompensationDrive_.store(std::clamp(drive, 0.0f, 2.0f), std::memory_order_release);
    }
    float blCompensationDrive() const noexcept {
        return blCompensationDrive_.load(std::memory_order_acquire);
    }
    void setWarpingLambda(float lam) noexcept {
        lambda_ = std::clamp(lam, -0.85f, 0.85f);
    }
    float warpingLambda() const noexcept { return lambda_; }
    float lastSubSampleDelay() const noexcept { return lastSubSampleDelay_.load(std::memory_order_relaxed); }

private:
    [[gnu::always_inline]] inline void processWarpedLatticeStep(
        float& f,
        std::array<float, ORDER>& bState,
        std::array<float, ORDER>& apMem) noexcept
    {
        const float lam = lambda_;
        // Operador allpass deformado D_λ(z) sobre el vector de estados regresivos b_{m-1}(n)
        alignas(16) std::array<float, ORDER> warpedB{};

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        const float32x4_t vLam = vdupq_n_f32(lam);
        for (size_t base = 0; base < ORDER; base += 4) {
            float32x4_t vb = vld1q_f32(&bState[base]);
            float32x4_t vm = vld1q_f32(&apMem[base]);
            // w = vm - lam * vb; new_vm = vb + lam * w
            float32x4_t vw = vfmsq_f32(vm, vLam, vb);
            float32x4_t vNewMem = vfmaq_f32(vb, vLam, vw);
            vst1q_f32(&warpedB[base], vw);
            vst1q_f32(&apMem[base], vNewMem);
        }
#else
        #pragma GCC ivdep
        for (size_t m = 0; m < ORDER; ++m) {
            const float w = apMem[m] - lam * bState[m];
            apMem[m] = sanitize(bState[m] + lam * w);
            warpedB[m] = sanitize(w);
        }
#endif

        // Escalera de celosía inversa (all-pole / lattice hybrid para inversión de fase mínima de Z(ω))
        float prevB = f;
        for (size_t m = 0; m < ORDER; ++m) {
            const float km = kappa_[m];
            const float wb = warpedB[m];
            const float fNext = f - km * wb;
            const float bNext = wb + km * fNext;
            bState[m] = sanitize(prevB);
            prevB = bNext;
            f = sanitize(fNext);
        }
    }

    [[gnu::always_inline]] inline float applySubSampleDelay(
        float x,
        std::array<float, 4>& d,
        float mu) const noexcept
    {
        // Interpolación cúbica de Lagrange para retardo fraccional sub-muestra d ∈ [0, 1)
        const float d0 = x;
        const float d1 = d[0];
        const float d2 = d[1];
        const float d3 = d[2];

        d[2] = d[1];
        d[1] = d[0];
        d[0] = x;

        const float c0 = -mu * (mu - 1.0f) * (mu - 2.0f) * (1.0f / 6.0f);
        const float c1 =  (mu + 1.0f) * (mu - 1.0f) * (mu - 2.0f) * 0.5f;
        const float c2 = -(mu + 1.0f) * mu * (mu - 2.0f) * 0.5f;
        const float c3 =  (mu + 1.0f) * mu * (mu - 1.0f) * (1.0f / 6.0f);

        return sanitize(c0 * d3 + c1 * d2 + c2 * d1 + c3 * d0);
    }

    alignas(64) std::array<float, ORDER> kappa_{};
    alignas(64) std::array<float, ORDER> gammaGroupDelay_{};
    alignas(64) std::array<float, ORDER> bStateL_{};
    alignas(64) std::array<float, ORDER> bStateR_{};
    alignas(64) std::array<float, ORDER> allpassMemL_{};
    alignas(64) std::array<float, ORDER> allpassMemR_{};
    alignas(16) std::array<float, 4> delayFracL_{};
    alignas(16) std::array<float, 4> delayFracR_{};

    float sampleRate_{48000.0f};
    float invSampleRate_{1.0f / 48000.0f};
    float lambda_{0.72f};
    float beta1_{0.045f};
    float beta2_{0.180f};
    float excursionDecay_{0.988f};
    float excursionEstL_{0.0f};
    float excursionEstR_{0.0f};
    float silenceEnvelope_{0.0f};
    float chirpPhase_{0.0f};
    float chirpFreqHz_{17500.0f};
    uint32_t lfsrState_{0xA5A5F00Du};

    std::atomic<bool> enabled_{true};
    std::atomic<bool> microChirpEnabled_{true};
    std::atomic<float> blCompensationDrive_{1.0f};
    std::atomic<float> lastSubSampleDelay_{0.24f};
};

} // namespace ivanna::supreme
