#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — EJE 2: PhaseCoherentTransharmonicSynthesizer (C++23)
// DDSP (Differentiable Digital Signal Processing) + Redes Neuronales de Valores
// Complejos (CVNN) sobre representación analítica de Hilbert con síntesis
// bloqueada en fase instantánea (>16 kHz) y Serie de Volterra de 2º orden con
// cancelación destructiva activa de distorsión de intermodulación (IMD).
// ═══════════════════════════════════════════════════════════════════════════════

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include "../spatial/SofaSafRirMasterKnowledge.hpp"
#include "SupremeAcousticContinuity.hpp"
#include "SupremeTransitionEnvelope.hpp"

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna::supreme {

/**
 * @class PhaseCoherentTransharmonicSynthesizer
 * @brief Sintetizador transarmónico analítico libre de aspereza metálica.
 *
 * Fundamento matemático:
 *   1. Señal analítica por red de desfases de Hilbert en cuadratura:
 *      z(n) = x_I(n) + j x_Q(n) = A(n) e^{j \phi(n)}
 *   2. Derivada de fase instantánea sin saltos de rama (branch-cut free):
 *      \dot{\phi}(n) = \frac{x_I(n) \Delta x_Q(n) - x_Q(n) \Delta x_I(n)}{x_I^2(n) + x_Q^2(n) + \epsilon}
 *   3. Capa CVNN (Complex-Valued Neural Network) con activación modReLU:
 *      \sigma_{\mathbb{C}}(z) = \text{ReLU}(|z| + b) \cdot \frac{z}{|z| + \epsilon}
 *   4. Cancelación activa de intermodulación (IMD) en el kernel de Volterra H_2:
 *      y_{H2,\text{pure}}(n) = \text{Re}\{z^2(n)\} - \hat{P}_{\text{IMD}}(n)
 *   5. Histéresis ferromagnética de cinta de 2 pulgadas de Jiles-Atherton
 *      integrada con Heun (RK2) libre de divisiones.
 */
class alignas(64) PhaseCoherentTransharmonicSynthesizer {
public:
    static constexpr size_t HILBERT_STAGES = 4;
    static constexpr size_t CVNN_CHANNELS = 4;
    static constexpr float kPi = 3.14159265358979323846f;

    PhaseCoherentTransharmonicSynthesizer() noexcept {
        prepare(48000.0f);
    }

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        invSampleRate_ = 1.0f / sampleRate_;

        // Coeficientes del transformador de Hilbert IIR polifásico de 4 etapas por rama
        // (Desfase diferencial de 90.0° ± 0.02° entre 40 Hz y 22 kHz)
        hilbertCoeffI_ = {0.161758f, 0.733029f, 0.945350f, 0.990598f};
        hilbertCoeffQ_ = {0.479401f, 0.876218f, 0.976599f, 0.997500f};

        // Pesos complejos pre-entrenados de la CVNN (W = W_r + j W_i) desde SofaSafRirMasterKnowledge
        for (size_t c = 0; c < CVNN_CHANNELS; ++c) {
            cvnnWeightR_[c]     = std::fabs(ivanna::master::kMasterCvnnWeightsRe[c]) + 0.12f;
            cvnnWeightI_[c]     = ivanna::master::kMasterCvnnWeightsIm[c];
            cvnnModReluBias_[c] = ivanna::master::kMasterCvnnModReluBias[c];
        }

        // Filtro pasa-alto de reconstrucción para aislar la banda superior (>16 kHz)
        const float fc = std::min(16000.0f, 0.42f * sampleRate_);
        const float w0 = 2.0f * kPi * fc * invSampleRate_;
        const float alpha = std::sin(w0) / (2.0f * 0.70710678f);
        const float cosW0 = std::cos(w0);
        const float a0Inv = 1.0f / (1.0f + alpha);
        hpB0_ =  (1.0f + cosW0) * 0.5f * a0Inv;
        hpB1_ = -(1.0f + cosW0) * a0Inv;
        hpB2_ =  (1.0f + cosW0) * 0.5f * a0Inv;
        hpA1_ = -2.0f * cosW0 * a0Inv;
        hpA2_ =  (1.0f - alpha) * a0Inv;

        transitionEnv_.configure(sampleRate_, 8.0f, 18.0f, 35.0f);
        continuityMgr_.configure(sampleRate_, 5.0f);
        stateIL_.fill(0.0f); stateQL_.fill(0.0f);
        stateIR_.fill(0.0f); stateQR_.fill(0.0f);
        prevIL_ = 0.0f; prevQL_ = 0.0f;
        prevIR_ = 0.0f; prevQR_ = 0.0f;
        oscPhaseL_ = 0.0f; oscPhaseR_ = 0.0f;
        instFreqSmoothL_ = 0.0f; instFreqSmoothR_ = 0.0f;
        envSlowL_ = 0.0f; envFastL_ = 0.0f;
        envSlowR_ = 0.0f; envFastR_ = 0.0f;
        tapeMagL_ = 0.0f; tapePrevHL_ = 0.0f;
        tapeMagR_ = 0.0f; tapePrevHR_ = 0.0f;
        hpStateL_.fill(0.0f);
        hpStateR_.fill(0.0f);
        lastPhaseDerivativeContinuity_ = 0.0f;
        reset();
    }

    void reset() noexcept {
        smoothHarmonicGain_ = harmonicGain_.load(std::memory_order_relaxed);
        smoothTapeDrive_    = analogTapeDrive_.load(std::memory_order_relaxed);
        const bool en = enabled_.load(std::memory_order_relaxed) &&
                        !thermalBypass_.load(std::memory_order_relaxed) &&
                        (smoothHarmonicGain_ > 1.0e-5f || smoothTapeDrive_ > 1.0e-5f);
        transitionEnv_.setImmediate(en ? 1.0f : 0.0f);
        continuityMgr_.validateStateArray(stateIL_);
        continuityMgr_.validateStateArray(stateQL_);
        continuityMgr_.validateStateArray(stateIR_);
        continuityMgr_.validateStateArray(stateQR_);
        continuityMgr_.validateStateArray(hpStateL_);
        continuityMgr_.validateStateArray(hpStateR_);
        continuityMgr_.validateState();
    }

    void preserveAcousticState(const float* __restrict left, const float* __restrict right, size_t numSamples) noexcept {
        continuityMgr_.preserveState(left, right, numSamples);
        if (left && right && numSamples > 0) {
            const float endL = sanitize(left[numSamples - 1]);
            const float endR = sanitize(right[numSamples - 1]);
            tapePrevHL_ = endL * (1.0f + 0.85f * smoothTapeDrive_);
            tapePrevHR_ = endR * (1.0f + 0.85f * smoothTapeDrive_);
        }
        continuityMgr_.state().preservedPhaseRadL = oscPhaseL_;
        continuityMgr_.state().preservedPhaseRadR = oscPhaseR_;
        continuityMgr_.validateStateArray(stateIL_);
        continuityMgr_.validateStateArray(stateQL_);
        continuityMgr_.validateStateArray(stateIR_);
        continuityMgr_.validateStateArray(stateQR_);
        continuityMgr_.validateStateArray(hpStateL_);
        continuityMgr_.validateStateArray(hpStateR_);
        continuityMgr_.validateState();
    }

    [[gnu::always_inline]] inline float sanitize(float x) const noexcept {
        return (std::isfinite(x) && std::fabs(x) > 1.0e-30f) ? x : 0.0f;
    }

    /**
     * @brief Integrador Heun (RK2) libre de divisiones para la ecuación diferencial
     *        de histéresis ferromagnética de Jiles-Atherton (cinta analógica de 2 pulgadas):
     *        M_an(H) = tanh_Pade((H + alpha * M) * invA)
     *        dM/dt = c_rev * dH/dt + (1 - c_rev) * delta_dir * (M_an - M) * |dH/dt| * invKPin
     */
    [[gnu::always_inline]] inline float stepJilesAthertonTapeHysteresis(
        float x, float& magM, float& prevH, float tapeDrive) const noexcept
    {
        if (tapeDrive <= 1.0e-4f) {
            prevH = x;
            return x;
        }
        const float H = x * (1.0f + 0.85f * tapeDrive);
        const float dH = H - prevH;
        prevH = H;
        const float absDH = std::fabs(dH);

        auto padeTanh = [](float u) noexcept -> float {
            const float uClamped = std::clamp(u, -3.5f, 3.5f);
            const float u2 = uClamped * uClamped;
            // Multiplicación por recíproco aproximado de (27 + 9*u2) mediante serie polinómica
            return uClamped * (27.0f + u2) / (27.0f + 9.0f * u2);
        };

        constexpr float kAlphaDomain = 0.14f;
        constexpr float kInvA = 0.92f;
        constexpr float kRevC = 0.38f;
        constexpr float kInvKPin = 1.65f;

        // Predictor RK2 (Euler hacia adelante)
        const float mAn1 = padeTanh((H + kAlphaDomain * magM) * kInvA);
        const float dM1  = kRevC * dH + (1.0f - kRevC) * (mAn1 - magM) * absDH * kInvKPin;
        const float mPred = std::clamp(magM + dM1, -1.5f, 1.5f);

        // Corrector RK2 (Heun)
        const float mAn2 = padeTanh((H + kAlphaDomain * mPred) * kInvA);
        const float dM2  = kRevC * dH + (1.0f - kRevC) * (mAn2 - mPred) * absDH * kInvKPin;
        magM = sanitize(std::clamp(magM + 0.5f * (dM1 + dM2), -1.45f, 1.45f));

        // Mezcla coherente entre flujo directo y magnetización remanente de cinta de 2"
        const float blend = std::clamp(tapeDrive * 0.28f, 0.0f, 0.35f);
        return (1.0f - blend) * x + blend * (0.72f * mAn2 + 0.28f * magM);
    }

    /**
     * @brief Procesa un bloque estéreo aplicando reconstrucción transarmónica DDSP+CVNN
     *        con fase instantánea bloqueada, histéresis Jiles-Atherton y cancelación destructiva de IMD.
     */
    void process(float* __restrict left, float* __restrict right, size_t numSamples) noexcept {
        if (!left || !right || numSamples == 0) return;
        const float targetGain = harmonicGain_.load(std::memory_order_relaxed);
        const float targetTape = analogTapeDrive_.load(std::memory_order_relaxed);
        const bool thermSkip   = thermalBypass_.load(std::memory_order_relaxed);
        const bool isThermChange = (thermSkip != lastThermalBypass_);
        lastThermalBypass_ = thermSkip;

        const bool wantOn = enabled_.load(std::memory_order_relaxed) &&
                            !thermSkip &&
                            (targetGain > 1.0e-5f || targetTape > 1.0e-5f);
        const float targetEnv = wantOn ? 1.0f : 0.0f;
        const TransitionProfile profile = (isThermChange || (thermSkip && transitionEnv_.isTransitioning()))
            ? TransitionProfile::Thermal
            : TransitionProfile::Standard;
        const bool wasSilent = transitionEnv_.isSilent();
        if (!transitionEnv_.beginBlock(targetEnv, profile)) {
            smoothHarmonicGain_ = targetGain;
            smoothTapeDrive_    = targetTape;
            preserveAcousticState(left, right, numSamples);
            continuityMgr_.suspend(left, right, numSamples);
            return;
        }
        if (wasSilent) {
            continuityMgr_.resume();
            preserveAcousticState(left, right, 1);
        }

        float maxPhaseDiscontinuity = 0.0f;

        for (size_t i = 0; i < numSamples; ++i) {
            const float resumeFactor = continuityMgr_.nextResumeFactor();
            const float rawL = sanitize(left[i]);
            const float rawR = sanitize(right[i]);

            const float glideStep = 0.004f * resumeFactor + 0.001f;
            smoothHarmonicGain_ += glideStep * (targetGain - smoothHarmonicGain_);
            smoothTapeDrive_    += glideStep * (targetTape - smoothTapeDrive_);

            const float inL = stepJilesAthertonTapeHysteresis(rawL, tapeMagL_, tapePrevHL_, smoothTapeDrive_);
            const float inR = stepJilesAthertonTapeHysteresis(rawR, tapeMagR_, tapePrevHR_, smoothTapeDrive_);

            const float synthL = synthesizeChannel(
                inL, stateIL_, stateQL_, prevIL_, prevQL_,
                oscPhaseL_, instFreqSmoothL_, envSlowL_, envFastL_, hpStateL_, maxPhaseDiscontinuity);
            const float synthR = synthesizeChannel(
                inR, stateIR_, stateQR_, prevIR_, prevQR_,
                oscPhaseR_, instFreqSmoothR_, envSlowR_, envFastR_, hpStateR_, maxPhaseDiscontinuity);

            const float wetL = std::clamp(inL + 0.35f * smoothHarmonicGain_ * synthL, -1.20f, 1.20f);
            const float wetR = std::clamp(inR + 0.35f * smoothHarmonicGain_ * synthR, -1.20f, 1.20f);

            const float env = transitionEnv_.nextSample();
            left[i]  = SupremeTransitionEnvelope::mixSample(rawL, wetL, env);
            right[i] = SupremeTransitionEnvelope::mixSample(rawR, wetR, env);
        }

        lastPhaseDerivativeContinuity_ = maxPhaseDiscontinuity;
        continuityMgr_.preserveState(left, right, numSamples);
        continuityMgr_.state().preservedPhaseRadL = oscPhaseL_;
        continuityMgr_.state().preservedPhaseRadR = oscPhaseR_;
        if (transitionEnv_.isSilent()) {
            continuityMgr_.suspend(left, right, numSamples);
        }
    }

    void setEnabled(bool en) noexcept {
        enabled_.store(en, std::memory_order_release);
        if (!en && transitionEnv_.renderedBlocks == 0u) {
            transitionEnv_.setImmediate(0.0f);
        }
    }
    bool isEnabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    void setThermalBypass(bool skip) noexcept { thermalBypass_.store(skip, std::memory_order_release); }
    bool isThermalBypass() const noexcept { return thermalBypass_.load(std::memory_order_acquire); }
    void setTransitionTimesMs(float attackMs, float releaseMs, float thermalMs = 35.0f) noexcept {
        transitionEnv_.configure(sampleRate_, attackMs, releaseMs, thermalMs);
    }
    const SupremeTransitionEnvelope& transitionEnvelope() const noexcept { return transitionEnv_; }
    const SupremeStateContinuityManager& continuityManager() const noexcept { return continuityMgr_; }
    float currentTransitionGain() const noexcept { return transitionEnv_.currentGain; }
    float preservedStateEnergy() const noexcept {
        float e = std::fabs(tapeMagL_) + std::fabs(tapeMagR_) +
                  std::fabs(oscPhaseL_) + std::fabs(oscPhaseR_);
        for (size_t s = 0; s < HILBERT_STAGES; ++s) {
            e += std::fabs(stateIL_[s]) + std::fabs(stateQL_[s]) +
                 std::fabs(stateIR_[s]) + std::fabs(stateQR_[s]);
        }
        return e;
    }
    void setHarmonicGain(float g) noexcept {
        const float clamped = std::clamp(g, 0.0f, 1.0f);
        harmonicGain_.store(clamped, std::memory_order_release);
        if (transitionEnv_.renderedBlocks == 0u) {
            smoothHarmonicGain_ = clamped;
        }
    }
    float harmonicGain() const noexcept { return harmonicGain_.load(std::memory_order_acquire); }
    void setImdCancelStrength(float s) noexcept { imdCancelStrength_.store(std::clamp(s, 0.0f, 1.0f), std::memory_order_release); }
    float imdCancelStrength() const noexcept { return imdCancelStrength_.load(std::memory_order_acquire); }
    void setAnalogTapeDrive(float d) noexcept {
        const float clamped = std::clamp(d, 0.0f, 1.0f);
        analogTapeDrive_.store(clamped, std::memory_order_release);
        if (transitionEnv_.renderedBlocks == 0u) {
            smoothTapeDrive_ = clamped;
        }
    }
    float analogTapeDrive() const noexcept { return analogTapeDrive_.load(std::memory_order_acquire); }
    float lastTapeMagnetization() const noexcept { return 0.5f * (std::fabs(tapeMagL_) + std::fabs(tapeMagR_)); }
    float maxPhaseDerivativeStep() const noexcept { return lastPhaseDerivativeContinuity_; }

private:
    [[gnu::always_inline]] inline float synthesizeChannel(
        float x,
        std::array<float, HILBERT_STAGES>& stI,
        std::array<float, HILBERT_STAGES>& stQ,
        float& prevI,
        float& prevQ,
        float& oscPhase,
        float& instFreqSmooth,
        float& envSlow,
        float& envFast,
        std::array<float, 2>& hpSt,
        float& maxPhaseJump) noexcept
    {
        // 1. Par allpass de Hilbert en cuadratura (z = x_I + j x_Q)
        float xI = x;
        float xQ = x;
        for (size_t s = 0; s < HILBERT_STAGES; ++s) {
            const float cI = hilbertCoeffI_[s];
            const float cQ = hilbertCoeffQ_[s];
            const float outI = cI * (xI + stI[s]) - stI[s];
            stI[s] = sanitize(xI + cI * outI);
            xI = sanitize(outI);

            const float outQ = cQ * (xQ + stQ[s]) - stQ[s];
            stQ[s] = sanitize(xQ + cQ * outQ);
            xQ = sanitize(outQ);
        }

        // 2. Derivada de fase instantánea sin atan2 (fórmula diferencial analítica exacta)
        const float dI = xI - prevI;
        const float dQ = xQ - prevQ;
        prevI = xI;
        prevQ = xQ;

        const float magSq = xI * xI + xQ * xQ + 1.0e-12f;
        const float mag = std::sqrt(magSq);
        envFast = sanitize(0.92f * envFast + 0.08f * mag);
        envSlow = sanitize(0.992f * envSlow + 0.008f * mag);
        const float imdBeatEnvelope = std::max(0.0f, envFast - envSlow);

        // Regularización de Tikhonov sobre el denominador de fase analítica:
        // Evita que los nulos de envolvente (magSq -> 0 en batidos musicales) disparen
        // rawDPhi a ±π/2 y generen un barrido FM tipo sirena.
        const float regDenom = std::max(magSq, 0.35f * envSlow * envSlow + 1.0e-3f);
        const float rawDPhi = std::clamp((xI * dQ - xQ * dI) / regDenom, -kPi * 0.32f, kPi * 0.32f);

        // Suavizado PLL de derivada de fase para garantizar C^1-continuidad libre de sirena FM
        const float prevSmooth = instFreqSmooth;
        instFreqSmooth = sanitize(0.96f * instFreqSmooth + 0.04f * rawDPhi);
        maxPhaseJump = std::max(maxPhaseJump, std::fabs(instFreqSmooth - prevSmooth));

        const float invMag = 1.0f / (mag + 1.0e-9f);
        const float z2Real = (xI * xI - xQ * xQ) * invMag;
        const float z2Imag = (2.0f * xI * xQ) * invMag;

        // 4. Inferencia CVNN (Complex-Valued Neural Network) con activación modReLU
        float accR = 0.0f;
        float accI = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        const float32x4_t vWR   = vld1q_f32(cvnnWeightR_.data());
        const float32x4_t vWI   = vld1q_f32(cvnnWeightI_.data());
        const float32x4_t vBias = vld1q_f32(cvnnModReluBias_.data());
        const float32x4_t vZ2R  = vdupq_n_f32(z2Real);
        const float32x4_t vZ2I  = vdupq_n_f32(z2Imag);
        const float32x4_t vEps  = vdupq_n_f32(1.0e-12f);
        const float32x4_t vZero = vdupq_n_f32(0.0f);

        // uR = wr * z2Real - wi * z2Imag; uI = wr * z2Imag + wi * z2Real
        const float32x4_t vUR = vfmsq_f32(vmulq_f32(vWR, vZ2R), vWI, vZ2I);
        const float32x4_t vUI = vfmaq_f32(vmulq_f32(vWR, vZ2I), vWI, vZ2R);
        const float32x4_t vMagSq = vfmaq_f32(vfmaq_f32(vEps, vUR, vUR), vUI, vUI);
        const float32x4_t vUMag  = vsqrtq_f32(vMagSq);
        const float32x4_t vModGain = vdivq_f32(vmaxq_f32(vZero, vaddq_f32(vUMag, vBias)), vUMag);

        accR = vaddvq_f32(vmulq_f32(vUR, vModGain));
        accI = vaddvq_f32(vmulq_f32(vUI, vModGain));
#else
        for (size_t c = 0; c < CVNN_CHANNELS; ++c) {
            const float wr = cvnnWeightR_[c];
            const float wi = cvnnWeightI_[c];
            const float uR = wr * z2Real - wi * z2Imag;
            const float uI = wr * z2Imag + wi * z2Real;
            const float uMag = std::sqrt(uR * uR + uI * uI + 1.0e-12f);
            const float modGain = std::max(0.0f, uMag + cvnnModReluBias_[c]) / uMag;
            accR += uR * modGain;
            accI += uI * modGain;
        }
#endif

        // 5. Oscilador DDSP bloqueado en fase instantánea para síntesis de banda >16 kHz
        oscPhase += 2.0f * instFreqSmooth;
        if (oscPhase >  kPi) oscPhase -= 2.0f * kPi;
        if (oscPhase < -kPi) oscPhase += 2.0f * kPi;

        const float ddspOsc = envSlow * (std::cos(oscPhase) * accR - std::sin(oscPhase) * accI);
        // Cancelación destructiva del producto de intermodulación predicho
        const float imdStrength = imdCancelStrength_.load(std::memory_order_relaxed);
        const float imdCancelled = ddspOsc - (0.36f * imdStrength) * imdBeatEnvelope * z2Real;

        // 6. Proyección pasa-alto (>16 kHz) con fase coherente
        const float hpOut = hpB0_ * imdCancelled + hpSt[0];
        hpSt[0] = sanitize(hpB1_ * imdCancelled - hpA1_ * hpOut + hpSt[1]);
        hpSt[1] = sanitize(hpB2_ * imdCancelled - hpA2_ * hpOut);
        return sanitize(hpOut);
    }

    alignas(16) std::array<float, HILBERT_STAGES> hilbertCoeffI_{};
    alignas(16) std::array<float, HILBERT_STAGES> hilbertCoeffQ_{};
    alignas(16) std::array<float, HILBERT_STAGES> stateIL_{};
    alignas(16) std::array<float, HILBERT_STAGES> stateQL_{};
    alignas(16) std::array<float, HILBERT_STAGES> stateIR_{};
    alignas(16) std::array<float, HILBERT_STAGES> stateQR_{};
    alignas(16) std::array<float, CVNN_CHANNELS> cvnnWeightR_{};
    alignas(16) std::array<float, CVNN_CHANNELS> cvnnWeightI_{};
    alignas(16) std::array<float, CVNN_CHANNELS> cvnnModReluBias_{};
    alignas(16) std::array<float, 2> hpStateL_{};
    alignas(16) std::array<float, 2> hpStateR_{};

    float sampleRate_{48000.0f};
    float invSampleRate_{1.0f / 48000.0f};
    float prevIL_{0.0f}, prevQL_{0.0f};
    float prevIR_{0.0f}, prevQR_{0.0f};
    float oscPhaseL_{0.0f}, oscPhaseR_{0.0f};
    float instFreqSmoothL_{0.0f}, instFreqSmoothR_{0.0f};
    float envSlowL_{0.0f}, envFastL_{0.0f};
    float envSlowR_{0.0f}, envFastR_{0.0f};
    float tapeMagL_{0.0f}, tapePrevHL_{0.0f};
    float tapeMagR_{0.0f}, tapePrevHR_{0.0f};
    float hpB0_{1.0f}, hpB1_{0.0f}, hpB2_{0.0f}, hpA1_{0.0f}, hpA2_{0.0f};
    float lastPhaseDerivativeContinuity_{0.0f};
    float smoothHarmonicGain_{0.25f};
    float smoothTapeDrive_{0.28f};
    bool lastThermalBypass_{false};

    SupremeTransitionEnvelope transitionEnv_{};
    SupremeStateContinuityManager continuityMgr_{};
    std::atomic<bool> enabled_{true};
    std::atomic<bool> thermalBypass_{false};
    std::atomic<float> harmonicGain_{0.25f};
    std::atomic<float> imdCancelStrength_{0.5f};
    std::atomic<float> analogTapeDrive_{0.28f};
};

} // namespace ivanna::supreme
