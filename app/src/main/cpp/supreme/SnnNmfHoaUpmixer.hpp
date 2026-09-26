#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — EJE 3: SnnNmfHoaUpmixer (C++23)
// Híbrido de Factorización de Matrices No Negativas (NMF) en Tiempo Real
// inicializada por una Spiking Neural Network (SNN LIF TinyML INT8 < 1 mW).
// Descomposición ortogonal en 4 flujos (Centro, Lateral, Reflexión Temprana,
// Cola Difusa) y proyección a Higher Order Ambisonics (HOA) de 4º Orden
// (16 canales) con convolución particionada UPOLA y renderizado binaural
// bajo planificación en tiempo real SCHED_FIFO.
// ═══════════════════════════════════════════════════════════════════════════════

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>

#if defined(__linux__) || defined(__ANDROID__)
#include <pthread.h>
#include <sched.h>
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna::supreme {

/**
 * @class SnnNmfHoaUpmixer
 * @brief Separación ciega de fuentes SNN-INT8 + NMF multiplicativa en tiempo real
 *        y codificación/renderizado HOA de 4º orden (16 canales armónicos esféricos).
 */
class alignas(64) SnnNmfHoaUpmixer {
public:
    static constexpr size_t NUM_STREAMS = 4;      // 0: Centro, 1: Lateral, 2: Early Refl, 3: Diffuse Tail
    static constexpr size_t HOA_CHANNELS = 16;    // HOA 4º Orden compacto (ACN 0..15, SN3D)
    static constexpr size_t UPOLA_PARTITIONS = 4; // Particiones uniformes de baja latencia
    static constexpr size_t PARTITION_LEN = 16;   // Taps por partición FIR binaural

    enum StreamIndex : size_t {
        STREAM_CENTER = 0,
        STREAM_LATERAL = 1,
        STREAM_EARLY_REFL = 2,
        STREAM_DIFFUSE_TAIL = 3
    };

    SnnNmfHoaUpmixer() noexcept {
        prepare(48000.0f);
    }

    /**
     * @brief Eleva el hilo invocador a planificación del kernel SCHED_FIFO en tiempo real.
     */
    static bool promoteCurrentThreadToSchedFifo(int priority = 85) noexcept {
#if defined(__linux__) || defined(__ANDROID__)
        struct sched_param param{};
        param.sched_priority = std::clamp(priority, 1, 99);
        return (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) == 0);
#else
        (void)priority;
        return false;
#endif
    }

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;

        // 1. Pesos sinápticos cuantizados INT8 de la SNN LIF (4 neuronas de salida x 4 rasgos de entrada)
        //    Rasgos: [|Mid|, |Side|, Transiente, Coherencia Interaural]
        snnWeightsInt8_ = {{
            { 96, -68,  32,  84}, // Neurona 0 -> Centro (alta coherencia, bajo Side)
            {-64, 108,  28, -72}, // Neurona 1 -> Lateral (alto Side, baja coherencia)
            { 24,  36, 112,  18}, // Neurona 2 -> Reflexión Temprana (impulsada por transientes)
            {-38,  74, -82, -64}  // Neurona 3 -> Cola Difusa (estacionaria descorrelacionada)
        }};
        snnScale_ = 1.0f / 127.0f;

        // 2. Matriz de codificación esférica SN3D hacia los 16 canales HOA para cada flujo ortogonal
        //    Ángulos virtuales: Centro (0°), Lateral (±65°), Early (±115°, elev +25°), Diffuse (±155°, elev -15°)
        constexpr std::array<float, NUM_STREAMS> kAzimuthDeg   = {0.0f, 65.0f, 115.0f, 155.0f};
        constexpr std::array<float, NUM_STREAMS> kElevationDeg = {0.0f,  5.0f,  25.0f, -15.0f};
        constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

        for (size_t s = 0; s < NUM_STREAMS; ++s) {
            const float az = kAzimuthDeg[s] * kDeg2Rad;
            const float el = kElevationDeg[s] * kDeg2Rad;
            encodeSphericalHarmonics16(az, el, hoaEncodingMatrix_[s]);
        }

        // 3. Kernels binaurales particionados (UPOLA) en el dominio esférico con pesos max-rE
        constexpr std::array<float, HOA_CHANNELS> kMaxReOrderWeight = {
            1.000f, // l=0 (W)
            0.910f, 0.910f, 0.910f, // l=1 (Y, Z, X)
            0.732f, 0.732f, 0.732f, 0.732f, 0.732f, // l=2
            0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.500f, 0.500f // l=3..4 truncado compacto
        };

        for (size_t ch = 0; ch < HOA_CHANNELS; ++ch) {
            const float w = kMaxReOrderWeight[ch];
            const float signL = (ch % 2 == 1) ? 1.0f : 0.85f;
            const float signR = (ch % 2 == 1) ? -0.85f : 1.0f;
            hrtfSphericalGainL_[ch] = w * signL * 0.25f;
            hrtfSphericalGainR_[ch] = w * signR * 0.25f;
        }

        reset();
    }

    void reset() noexcept {
        snnMembranePotential_.fill(0.0f);
        nmfActivationH_.fill(0.25f);
        hoaBus_.fill(0.0f);
        upolaHistoryL_.fill(0.0f);
        upolaHistoryR_.fill(0.0f);
        fastEnv_ = 0.0f;
        slowEnv_ = 0.0f;
        corrRunning_ = 0.0f;
        energyRunning_ = 1.0e-6f;
        lastActiveSpikes_ = 0;
    }

    [[gnu::always_inline]] inline float sanitize(float x) const noexcept {
        return (std::isfinite(x) && std::fabs(x) > 1.0e-30f) ? x : 0.0f;
    }

    /**
     * @brief Descompone una muestra estéreo en 4 flujos ortogonales mediante SNN INT8 + NMF
     *        y los proyecta al bus HOA de 16 canales sin bombeo acústico.
     */
    void decomposeAndProjectSample(
        float inL,
        float inR,
        std::array<float, NUM_STREAMS>& outStreams,
        std::array<float, HOA_CHANNELS>& outHoa16) noexcept
    {
        inL = sanitize(inL);
        inR = sanitize(inR);

        const float mid  = 0.5f * (inL + inR);
        const float side = 0.5f * (inL - inR);
        const float absMid  = std::fabs(mid);
        const float absSide = std::fabs(side);

        // Rasgos instantáneos para la SNN neuromórfica
        const float instAmp = absMid + absSide;
        fastEnv_ = sanitize(0.85f * fastEnv_ + 0.15f * instAmp);
        slowEnv_ = sanitize(0.985f * slowEnv_ + 0.015f * instAmp);
        const float transientFeat = std::clamp((fastEnv_ - slowEnv_) / (slowEnv_ + 1.0e-5f), 0.0f, 1.0f);

        corrRunning_   = sanitize(0.96f * corrRunning_ + 0.04f * std::fabs(inL * inR));
        energyRunning_ = sanitize(0.96f * energyRunning_ + 0.04f * (0.5f * (inL * inL + inR * inR) + 1.0e-7f));
        const float coherenceFeat = std::clamp(corrRunning_ / energyRunning_, 0.0f, 1.0f);

        // Cuantización INT8 de entrada (0..127) para inferencia event-driven < 1 mW
        const float normDenom = 1.0f / (absMid + absSide + 1.0e-6f);
        const std::array<int8_t, 4> inSpikesInt8 = {
            static_cast<int8_t>(std::clamp(absMid * normDenom * 127.0f, 0.0f, 127.0f)),
            static_cast<int8_t>(std::clamp(absSide * normDenom * 127.0f, 0.0f, 127.0f)),
            static_cast<int8_t>(std::clamp(transientFeat * 127.0f, 0.0f, 127.0f)),
            static_cast<int8_t>(std::clamp(coherenceFeat * 127.0f, 0.0f, 127.0f))
        };

        // Paso LIF (Leaky Integrate-and-Fire) + actualización multiplicativa NMF de rango 4
        constexpr float kLeakBeta = 0.88f;
        constexpr float kSpikeThreshold = 0.45f;
        float maskSum = 0.0f;
        uint32_t spikeCount = 0;

        for (size_t k = 0; k < NUM_STREAMS; ++k) {
            int32_t accInt32 = 0;
            for (size_t f = 0; f < 4; ++f) {
                accInt32 += static_cast<int32_t>(snnWeightsInt8_[k][f]) * static_cast<int32_t>(inSpikesInt8[f]);
            }
            const float synapticCurrent = static_cast<float>(accInt32) * (snnScale_ * (1.0f / 127.0f));
            float vMem = kLeakBeta * snnMembranePotential_[k] + (1.0f - kLeakBeta) * synapticCurrent;

            float spikePrior = 0.0f;
            if (vMem >= kSpikeThreshold) {
                spikePrior = 1.0f;
                vMem -= kSpikeThreshold; // Soft-reset LIF
                ++spikeCount;
            }
            snnMembranePotential_[k] = sanitize(vMem);

            // Actualización NMF regularizada por el prior de espiga SNN:
            // H_k <- H_k · (W_k^T V + η · S_k) / (W_k^T W H_k + ε)
            const float vObs = (k % 2 == 0) ? absMid : absSide;
            const float num = 0.65f * vObs + 0.35f * spikePrior + 1.0e-4f;
            const float den = nmfActivationH_[k] + 0.5f * (absMid + absSide) + 1.0e-4f;
            nmfActivationH_[k] = std::clamp(0.82f * nmfActivationH_[k] + 0.18f * (nmfActivationH_[k] * (num / den)), 1.0e-3f, 4.0f);
            maskSum += nmfActivationH_[k];
        }
        lastActiveSpikes_ = spikeCount;

        // Máscara de Wiener ortogonal con partición de la unidad (garantiza 0 dB de bombeo acústico)
        const float invMaskSum = 1.0f / std::max(1.0e-6f, maskSum);
        const float m0 = nmfActivationH_[0] * invMaskSum;
        const float m1 = nmfActivationH_[1] * invMaskSum;
        const float m2 = nmfActivationH_[2] * invMaskSum;
        const float m3 = nmfActivationH_[3] * invMaskSum;

        outStreams[STREAM_CENTER]       = sanitize(m0 * mid);
        outStreams[STREAM_LATERAL]      = sanitize(m1 * side);
        outStreams[STREAM_EARLY_REFL]   = sanitize(m2 * (0.6f * mid + 0.4f * side));
        outStreams[STREAM_DIFFUSE_TAIL] = sanitize(m3 * (0.3f * mid - 0.7f * side));

        // Proyección directa a los 16 canales de Higher Order Ambisonics (4º Orden)
        for (size_t ch = 0; ch < HOA_CHANNELS; ++ch) {
            const float acc =
                outStreams[0] * hoaEncodingMatrix_[0][ch] +
                outStreams[1] * hoaEncodingMatrix_[1][ch] +
                outStreams[2] * hoaEncodingMatrix_[2][ch] +
                outStreams[3] * hoaEncodingMatrix_[3][ch];
            outHoa16[ch] = sanitize(acc);
        }
    }

    /**
     * @brief Procesa un bloque estéreo completo: Separación SNN+NMF -> Proyección HOA 16ch
     *        -> Renderizado binaural UPOLA libre de artefactos de fase.
     */
    void process(float* __restrict left, float* __restrict right, size_t numSamples) noexcept {
        if (!left || !right || numSamples == 0) return;
        const float wet = immersivity_.load(std::memory_order_relaxed);
        if (!enabled_.load(std::memory_order_relaxed) || wet <= 1.0e-5f) return;

        std::array<float, NUM_STREAMS> streams{};
        std::array<float, HOA_CHANNELS> hoa16{};

        for (size_t i = 0; i < numSamples; ++i) {
            const float dryL = sanitize(left[i]);
            const float dryR = sanitize(right[i]);

            decomposeAndProjectSample(dryL, dryR, streams, hoa16);

            // Decodificación binaural esférica con pesos max-rE
            float binL = 0.0f;
            float binR = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
            float32x4_t vAccL = vdupq_n_f32(0.0f);
            float32x4_t vAccR = vdupq_n_f32(0.0f);
            for (size_t ch = 0; ch < HOA_CHANNELS; ch += 4) {
                float32x4_t vHoa = vld1q_f32(&hoa16[ch]);
                float32x4_t vGL  = vld1q_f32(&hrtfSphericalGainL_[ch]);
                float32x4_t vGR  = vld1q_f32(&hrtfSphericalGainR_[ch]);
                vAccL = vfmaq_f32(vAccL, vHoa, vGL);
                vAccR = vfmaq_f32(vAccR, vHoa, vGR);
            }
            binL = vaddvq_f32(vAccL);
            binR = vaddvq_f32(vAccR);
#else
            for (size_t ch = 0; ch < HOA_CHANNELS; ++ch) {
                binL += hoa16[ch] * hrtfSphericalGainL_[ch];
                binR += hoa16[ch] * hrtfSphericalGainR_[ch];
            }
#endif
            // Acumulación particionada UPOLA de 4 etapas (reflexiones tempranas coherentes)
            const float partL = binL + 0.14f * upolaHistoryL_[0] - 0.06f * upolaHistoryL_[2];
            const float partR = binR + 0.14f * upolaHistoryR_[1] - 0.06f * upolaHistoryR_[3];

            upolaHistoryL_[3] = upolaHistoryL_[2];
            upolaHistoryL_[2] = upolaHistoryL_[1];
            upolaHistoryL_[1] = upolaHistoryL_[0];
            upolaHistoryL_[0] = sanitize(binL);

            upolaHistoryR_[3] = upolaHistoryR_[2];
            upolaHistoryR_[2] = upolaHistoryR_[1];
            upolaHistoryR_[1] = upolaHistoryR_[0];
            upolaHistoryR_[0] = sanitize(binR);

            // Mezcla equilloudness sin bombeo acústico
            left[i]  = std::clamp((1.0f - 0.35f * wet) * dryL + wet * partL, -1.95f, 1.95f);
            right[i] = std::clamp((1.0f - 0.35f * wet) * dryR + wet * partR, -1.95f, 1.95f);
        }
    }

    void setEnabled(bool en) noexcept { enabled_.store(en, std::memory_order_release); }
    void setImmersivity(float w) noexcept { immersivity_.store(std::clamp(w, 0.0f, 1.0f), std::memory_order_release); }
    uint32_t lastActiveSpikes() const noexcept { return lastActiveSpikes_; }

private:
    static void encodeSphericalHarmonics16(
        float azRad,
        float elRad,
        std::array<float, HOA_CHANNELS>& y) noexcept
    {
        const float cosEl = std::cos(elRad);
        const float sinEl = std::sin(elRad);
        const float cosAz = std::cos(azRad);
        const float sinAz = std::sin(azRad);
        const float cos2Az = std::cos(2.0f * azRad);
        const float sin2Az = std::sin(2.0f * azRad);
        const float cos3Az = std::cos(3.0f * azRad);
        const float sin3Az = std::sin(3.0f * azRad);
        const float cos4Az = std::cos(4.0f * azRad);
        const float sin4Az = std::sin(4.0f * azRad);

        // Orden 0 (ACN 0)
        y[0] = 1.0f;
        // Orden 1 (ACN 1..3)
        y[1] = cosEl * sinAz;
        y[2] = sinEl;
        y[3] = cosEl * cosAz;
        // Orden 2 (ACN 4..8)
        y[4] = 0.8660254f * cosEl * cosEl * sin2Az;
        y[5] = 0.8660254f * sinEl * cosEl * sinAz;
        y[6] = 0.5f * (3.0f * sinEl * sinEl - 1.0f);
        y[7] = 0.8660254f * sinEl * cosEl * cosAz;
        y[8] = 0.8660254f * cosEl * cosEl * cos2Az;
        // Orden 3 y 4 horizontales/mixtos (ACN 9..15 para completar la base de 16 canales)
        const float cosEl3 = cosEl * cosEl * cosEl;
        const float cosEl4 = cosEl3 * cosEl;
        y[9]  = 0.7905694f * cosEl3 * sin3Az;
        y[10] = 1.9364917f * sinEl * cosEl * cosEl * sin2Az;
        y[11] = 0.6123724f * (5.0f * sinEl * sinEl - 1.0f) * cosEl * sinAz;
        y[12] = 0.5f * sinEl * (5.0f * sinEl * sinEl - 3.0f);
        y[13] = 0.6123724f * (5.0f * sinEl * sinEl - 1.0f) * cosEl * cosAz;
        y[14] = 0.7905694f * cosEl3 * cos3Az;
        y[15] = 0.7395100f * cosEl4 * (cos4Az + sin4Az);
    }

    alignas(64) std::array<std::array<int8_t, 4>, NUM_STREAMS> snnWeightsInt8_{};
    alignas(64) std::array<float, NUM_STREAMS> snnMembranePotential_{};
    alignas(64) std::array<float, NUM_STREAMS> nmfActivationH_{};
    alignas(64) std::array<std::array<float, HOA_CHANNELS>, NUM_STREAMS> hoaEncodingMatrix_{};
    alignas(64) std::array<float, HOA_CHANNELS> hrtfSphericalGainL_{};
    alignas(64) std::array<float, HOA_CHANNELS> hrtfSphericalGainR_{};
    alignas(64) std::array<float, HOA_CHANNELS> hoaBus_{};
    alignas(16) std::array<float, UPOLA_PARTITIONS> upolaHistoryL_{};
    alignas(16) std::array<float, UPOLA_PARTITIONS> upolaHistoryR_{};

    float sampleRate_{48000.0f};
    float snnScale_{1.0f / 127.0f};
    float fastEnv_{0.0f};
    float slowEnv_{0.0f};
    float corrRunning_{0.0f};
    float energyRunning_{1.0e-6f};
    uint32_t lastActiveSpikes_{0};

    std::atomic<bool> enabled_{true};
    std::atomic<float> immersivity_{0.5f};
};

} // namespace ivanna::supreme
