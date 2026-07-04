/*
 * ivanna_npe_jni.cpp
 * Implementación real de com.ivanna.omega.neuromorphic.IvannaNpeNative.
 * Carga: System.loadLibrary("omega_vibratory")
 *
 * Ensambla las piezas antes aisladas del motor neuromórfico:
 *   NHOEngine          → oscilador armónico no lineal (excitador/shaper)
 *   LIFNeuronPool<32>   → pool de neuronas spiking, una por banda coclear x4
 *   BiquadEnvelopeBank  → 8 bandas IIR → cues perceptuales (L,T,S,R)
 *   AutonomousBrain     → heurística CF → género + parámetros de Synthesizer
 *   Synthesizer         → doble búfer atómico + smoother + firma/clasificación
 *
 * Flujo de señal por muestra (process_sample):
 *   x → BiquadEnvelopeBank.extract() → cues (L,T,S,R)
 *     → LIFNeuronPool.tick(currents derivadas de cues) → spikes
 *     → NHOEngine.process_sample(x) → y (excitación armónica)
 *     → inhibición lateral (spikes/32) → y
 *     → compresión OHC (cues.L, ohc_compression) → y
 *     → AGC (target/rate) → y
 *     → master gain (dB) → y
 *     → ring buffer de scope
 *   Cada bloque: AutonomousBrain.processBlock(mono) conduce Synthesizer;
 *   Synthesizer.smoothTick() suaviza; firma/clasificación quedan listas
 *   para nativeGetSynthSignature / nativeGetSynthClassify.
 *
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 */

#include <jni.h>
#include <android/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <vector>

#include "../include/audio_thread_priority.h"
#include "../neuromorphic/nho_engine.hpp"
#include "../neuromorphic/lif_neuron_pool.hpp"
#include "../neuromorphic/biquad_envelope_bank.hpp"
#include "../neuromorphic/autonomous_brain.hpp"

#define LOG_TAG "IVANNA-NPE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)

// ── phase_oracle_velocity() — símbol externo definido en phase_oracle.cpp ────
// Nota: phase_oracle.cpp define la versión principal (Kalman cúbico).
// Esta versión era duplicada; se elimina para evitar error de linking.
extern "C" float phase_oracle_velocity();

// ── AUDIT FIX (AGC crackle) ─────────────────────────────────────────────────────
// Antes: yL/yR se escribían a outL/outR sin ningún techo tras el AGC + Master
// Gain, así que AGC rate alto (ganancia saltando hasta 8x) + Master Gain
// positivo producía clipping digital duro y crujidos audibles. Se reutiliza
// el mismo esquema de soft-knee limiter que ya usa audio_orchestrator.cpp
// (LIMITER_THRESH/LIMITER_CEIL) para mantener consistencia en todo el
// proyecto en lugar de introducir un limiter nuevo.
static constexpr float NPE_LIMITER_THRESH = 0.98855f;   // -0.1 dBFS
static constexpr float NPE_LIMITER_CEIL   = 0.989f;

static inline float npe_soft_limit(float x) noexcept {
    if (!std::isfinite(x)) return 0.0f;
    if (x > NPE_LIMITER_THRESH)  return NPE_LIMITER_CEIL  - (x - NPE_LIMITER_THRESH) * 0.1f;
    if (x < -NPE_LIMITER_THRESH) return -NPE_LIMITER_CEIL - (x + NPE_LIMITER_THRESH) * 0.1f;
    return x;
}

static inline void update_velocity(float x, float sample_rate) noexcept {
    // NOTA: Esta función actualiza velocidad local, pero la función
    // phase_oracle_velocity() se define ahora en phase_oracle.cpp para
    // evitar symbols duplicados en el linking.
    // Mantener esta lógica por si algún código en ivanna_npe la llama.
    (void)x; (void)sample_rate; // suppress unused warnings si no se llama
}

namespace ivanna_npe {

static constexpr int   SCOPE_SIZE   = 8192;
static constexpr float DB_MIN       = -60.f;

static inline float lin_to_db(float lin) noexcept {
    return 20.f * std::log10(std::max(lin, 1e-6f));
}
static inline float db_to_lin(float db) noexcept {
    return std::pow(10.f, db * 0.05f);
}

// ── Motor completo — una instancia por handle ──────────────────────────────
class Engine {
public:
    Engine(float sample_rate, int max_block_frames)
        : sample_rate_(sample_rate), max_block_frames_(max_block_frames) {
        envBank_.init(static_cast<uint32_t>(sample_rate));

        ivannanpe::LIFParams lp;
        lp.sample_rate = sample_rate;
        lifPool_.reset(lp);

        std::memset(scope_, 0, sizeof(scope_));
        reset();
    }

    void reset() noexcept {
        nho_.reset();
        envBank_.reset();
        synth_.reset();
        agc_gain_ = 1.f;
        rms_envelope_ = 0.f;
        scope_write_ = 0;
        std::memset(scope_, 0, sizeof(scope_));
        std::memset(&last_metrics_, 0, sizeof(last_metrics_));
    }

    void setBypass(bool bypass) noexcept { bypass_.store(bypass, std::memory_order_relaxed); }

    void setAGC(float target_db, float rate) noexcept {
        agc_target_db_ = target_db;
        agc_rate_ = std::clamp(rate, 0.f, 1.f);
    }

    void setEngineFlags(bool hrtf, bool cochlear, bool adapt) noexcept {
        hrtf_enabled_ = hrtf; cochlear_enabled_ = cochlear; adapt_enabled_ = adapt;
    }

    void setNeuroParams(float harmonic_gain, float lateral_inhib,
                         float ohc_compression, float master_gain_db) noexcept {
        harmonic_gain_ = harmonic_gain;
        lateral_inhib_ = std::clamp(lateral_inhib, 0.f, 1.f);
        ohc_compression_ = std::clamp(ohc_compression, 0.f, 1.f);
        master_gain_db_ = master_gain_db;
        nho_.set_harmonic_gain(harmonic_gain);
    }

    void setParameters(float alpha, float beta, float gamma, float delta,
                        float eta, float zeta, float sr_noise_floor,
                        float sync_threshold, float noise_gain_far, float phi,
                        float damping, float nonlinearity, float coupling) noexcept {
        nho_.set_alpha(alpha);
        nho_.set_beta(beta);
        nho_.set_mu(std::clamp(delta, 0.f, 1.f));
        nho_.set_wet(std::clamp(phi, 0.f, 1.f));
        nho_.set_harmonic_gain(std::clamp(nonlinearity, 0.f, 2.f));
        current_scale_   = std::clamp(gamma, 0.f, 4.f);
        eta_             = eta;
        zeta_inhib_      = std::clamp(zeta, 0.f, 1.f);
        noise_floor_lin_ = db_to_lin(sr_noise_floor);
        sync_threshold_  = sync_threshold;
        noise_gain_far_  = noise_gain_far;
        envBank_.env_release = std::clamp(0.010f * (1.f + damping), 0.001f, 0.2f);
        coupling_ = coupling;
    }

    // ── Procesamiento estéreo — corazón del motor ───────────────────────────
    void processStereo(const float* inL, const float* inR,
                        float* outL, float* outR, int n) noexcept {
        if (bypass_.load(std::memory_order_relaxed)) {
            std::memcpy(outL, inL, sizeof(float) * n);
            std::memcpy(outR, inR, sizeof(float) * n);
            return;
        }

        const std::clock_t t0 = std::clock();

        float mono_buf[512];
        const int mono_cap = static_cast<int>(sizeof(mono_buf) / sizeof(float));

        double sum_sq = 0.0, peak = 0.0;
        int spikes_total = 0;
        double t_acc = 0.0, s_acc = 0.0, r_acc = 0.0;

        for (int i = 0; i < n; ++i) {
            float xL = inL[i], xR = inR[i];

            ivanna::PerceptualCues cues{};
            if (cochlear_enabled_) {
                cues = envBank_.extract(xL, xR);
            }
            t_acc += cues.T; s_acc += cues.S; r_acc += cues.R;

            // ── LIF pool: currents derivadas de las 8 bandas coclear ────────
            int spikes = 0;
            if (adapt_enabled_) {
                std::array<float, 32> currents{};
                for (int b = 0; b < ivanna::BEB_BANDS; ++b) {
                    const float iL = envBank_.envL[b] * current_scale_ * 0.05f;
                    const float iR = envBank_.envR[b] * current_scale_ * 0.05f;
                    currents[b * 4 + 0] = iL;
                    currents[b * 4 + 1] = iR;
                    currents[b * 4 + 2] = iL * harmonic_gain_ * 0.5f;
                    currents[b * 4 + 3] = iR * harmonic_gain_ * 0.5f;
                }
                spikes = lifPool_.tick(currents);
            }
            spikes_total += spikes;

            // ── NHO: excitación armónica no lineal ───────────────────────────
            float yL, yR;
            nho_.process_sample(xL, xR, yL, yR);

            // ── Inhibición lateral (proporcional a tasa de disparo) ─────────
            const float inhib = 1.f - lateral_inhib_ * (static_cast<float>(spikes) / 32.f)
                                     - zeta_inhib_ * 0.1f * cues.S;
            const float inhib_clamped = std::clamp(inhib, 0.2f, 1.f);
            yL *= inhib_clamped; yR *= inhib_clamped;

            // ── Compresión OHC (soft-knee, dirigida por loudness cue) ───────
            if (ohc_compression_ > 0.f) {
                const float comp_gain = 1.f / (1.f + ohc_compression_ * cues.L * 4.f);
                yL *= comp_gain; yR *= comp_gain;
            }

            // ── HRTF ligero: ensanchado M/S dirigido por cue espacial ───────
            if (hrtf_enabled_) {
                const float mid = 0.5f * (yL + yR);
                const float side = 0.5f * (yL - yR);
                const float width = 1.f + 0.3f * cues.S;
                yL = mid + side * width;
                yR = mid - side * width;
            }

            // ── AGC ───────────────────────────────────────────────────────
            const float instRms = 0.5f * (std::fabs(yL) + std::fabs(yR));
            rms_envelope_ += 0.02f * (instRms - rms_envelope_);
            const float rmsDb = lin_to_db(rms_envelope_);
            const float desiredGain = db_to_lin(agc_target_db_ - rmsDb);
            // AUDIT FIX: slew-rate cap sobre el delta de ganancia por muestra.
            // Antes, con agc_rate_ ~1 el salto podía acercarse al 5% del gap
            // TOTAL entre ganancia actual y deseada (hasta 8x) en una sola
            // muestra, produciendo "pumping" audible además del riesgo de
            // clipping. Se mantiene la misma fórmula (no se borra) pero se
            // acota el paso máximo por muestra a 0.02 (~0.17dB/muestra a 48kHz),
            // así el slider sigue siendo más rápido al subirlo, sin saltar.
            const float rawStep = agc_rate_ * 0.05f * (desiredGain - agc_gain_);
            const float step = std::clamp(rawStep, -0.02f, 0.02f);
            agc_gain_ += step;
            agc_gain_ = std::clamp(agc_gain_, 0.25f, 4.f);
            yL *= agc_gain_; yR *= agc_gain_;

            // ── Master gain (dB) ─────────────────────────────────────────
            const float masterLin = db_to_lin(master_gain_db_);
            yL *= masterLin; yR *= masterLin;

            // ── Gate de piso de ruido ────────────────────────────────────
            if (std::fabs(yL) < noise_floor_lin_) yL = 0.f;
            if (std::fabs(yR) < noise_floor_lin_) yR = 0.f;

            // AUDIT FIX: limiter final — nunca existía un techo aquí. Este es
            // el causante directo del crujido al subir AGC rate + Master gain.
            yL = npe_soft_limit(yL);
            yR = npe_soft_limit(yR);

            outL[i] = yL; outR[i] = yR;

            const float mono = 0.5f * (outL[i] + outR[i]);
            update_velocity(mono, sample_rate_);
            scope_[scope_write_] = mono;
            scope_write_ = (scope_write_ + 1) % SCOPE_SIZE;

            if (i < mono_cap) mono_buf[i] = mono;

            const float mag = std::fabs(mono);
            sum_sq += static_cast<double>(mono) * mono;
            if (mag > peak) peak = mag;
        }

        // ── AutonomousBrain: análisis de ventana → género + Synthesizer ────
        const int brain_n = std::min(n, mono_cap);
        brain_.processBlock(mono_buf, brain_n, synth_);
        synth_.smoothTick(n, sample_rate_);

        const std::clock_t t1 = std::clock();
        const float cpu_seconds = static_cast<float>(t1 - t0) / CLOCKS_PER_SEC;
        const float block_seconds = static_cast<float>(n) / sample_rate_;
        const float cpu_load = block_seconds > 0.f ? cpu_seconds / block_seconds : 0.f;

        const float rms = std::sqrt(static_cast<float>(sum_sq / std::max(1, n)));
        const float invN = n > 0 ? 1.f / static_cast<float>(n) : 0.f;

        std::lock_guard<std::mutex> lk(metrics_mtx_);
        last_metrics_ = {
            std::clamp(cpu_load, 0.f, 4.f),
            rms,
            agc_gain_,
            spectralEntropy(),
            static_cast<float>(spikes_total) / block_seconds,
            static_cast<float>(t_acc * invN),
            static_cast<float>(s_acc * invN),
            static_cast<float>(r_acc * invN)
        };
    }

    void process(const float* in, float* out, int n) noexcept {
        // Motor es intrínsecamente estéreo — mono: duplicar canal.
        thread_local std::vector<float> tmpL, tmpR;
        tmpL.assign(in, in + n);
        tmpR.assign(in, in + n);
        std::vector<float> oL(n), oR(n);
        processStereo(tmpL.data(), tmpR.data(), oL.data(), oR.data(), n);
        for (int i = 0; i < n; ++i) out[i] = 0.5f * (oL[i] + oR[i]);
    }

    int snapshotScope(float* dst, int max_frames) noexcept {
        const int count = std::min(max_frames, SCOPE_SIZE);
        for (int i = 0; i < count; ++i) {
            const int idx = (scope_write_ - count + i + SCOPE_SIZE * 2) % SCOPE_SIZE;
            dst[i] = scope_[idx];
        }
        return count;
    }

    std::array<float, 8> getMetrics() noexcept {
        std::lock_guard<std::mutex> lk(metrics_mtx_);
        return last_metrics_;
    }

    const char* getDetectedGenre() const noexcept { return brain_.getLastGenre(); }
    void getSynthSignature(float out[5]) const noexcept { synth_.getSignature(out); }
    void getSynthClassify(float out[7]) const noexcept { synth_.classify(out); }

private:
    float spectralEntropy() const noexcept {
        // Entropía de Shannon sobre la energía de 8 bandas (BiquadEnvelopeBank).
        float total = 1e-12f;
        float e[ivanna::BEB_BANDS];
        for (int b = 0; b < ivanna::BEB_BANDS; ++b) {
            e[b] = envBank_.envL[b] * envBank_.envL[b] + envBank_.envR[b] * envBank_.envR[b];
            total += e[b];
        }
        float h = 0.f;
        for (int b = 0; b < ivanna::BEB_BANDS; ++b) {
            const float p = e[b] / total;
            if (p > 1e-9f) h -= p * std::log2(p);
        }
        // Normalizar a [0,1]: máxima entropía = log2(BEB_BANDS)
        return h / std::log2(static_cast<float>(ivanna::BEB_BANDS));
    }

    float sample_rate_;
    int   max_block_frames_;

    ivanna::NHOEngine            nho_;
    ivannanpe::LIFPool32         lifPool_;
    ivanna::BiquadEnvelopeBank   envBank_;
    ivanna::acoustic::Synthesizer synth_;
    ivanna::acoustic::AutonomousBrain brain_;

    std::atomic<bool> bypass_{false};
    bool  hrtf_enabled_ = true, cochlear_enabled_ = true, adapt_enabled_ = true;

    float harmonic_gain_ = 0.2f, lateral_inhib_ = 0.2f, ohc_compression_ = 0.3f,
          master_gain_db_ = 0.f;
    float current_scale_ = 1.f, eta_ = 0.f, zeta_inhib_ = 0.f;
    float noise_floor_lin_ = 0.f, sync_threshold_ = 0.5f, noise_gain_far_ = 0.f;
    float coupling_ = 0.f;

    float agc_target_db_ = -18.f, agc_rate_ = 0.3f, agc_gain_ = 1.f;
    float rms_envelope_ = 0.f;

    float scope_[SCOPE_SIZE]{};
    int   scope_write_ = 0;

    std::mutex metrics_mtx_;
    std::array<float, 8> last_metrics_{};
};

// ── Registro de instancias (handle = puntero) ──────────────────────────────
static std::mutex g_registry_mtx;
static std::atomic<Engine*> g_active{nullptr};  // última creada — para queries sin handle

static inline Engine* handle_to_engine(jlong h) noexcept {
    return reinterpret_cast<Engine*>(static_cast<intptr_t>(h));
}

} // namespace ivanna_npe

using namespace ivanna_npe;

extern "C" {

// ── Lifecycle ───────────────────────────────────────────────────────────────
JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeCreate(
    JNIEnv*, jclass, jfloat sr, jint maxBlk) {
    auto* eng = new Engine(sr, maxBlk);
    g_active.store(eng, std::memory_order_release);
    LOGI("nativeCreate: engine=%p sr=%.0f maxBlk=%d", (void*)eng, sr, maxBlk);
    return static_cast<jlong>(reinterpret_cast<intptr_t>(eng));
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDestroy(
    JNIEnv*, jclass, jlong handle) {
    Engine* eng = handle_to_engine(handle);
    if (g_active.load(std::memory_order_acquire) == eng) {
        g_active.store(nullptr, std::memory_order_release);
    }
    delete eng;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeReset(
    JNIEnv*, jclass, jlong handle) {
    if (Engine* eng = handle_to_engine(handle)) eng->reset();
}

// ── Processing ────────────────────────────────────────────────────────────────
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeProcess(
    JNIEnv* env, jclass, jlong handle, jobject inputBuffer, jobject outputBuffer, jint numFrames) {
    Engine* eng = handle_to_engine(handle);
    if (!eng) return;
    auto* in  = static_cast<float*>(env->GetDirectBufferAddress(inputBuffer));
    auto* out = static_cast<float*>(env->GetDirectBufferAddress(outputBuffer));
    if (!in || !out) return;
    ivanna::audio::enableAudioThreadFastMathOnce();
    eng->process(in, out, numFrames);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeProcessStereo(
    JNIEnv* env, jclass, jlong handle,
    jobject inL, jobject inR, jobject outL, jobject outR, jint numFrames) {
    Engine* eng = handle_to_engine(handle);
    if (!eng) return;
    auto* pInL  = static_cast<float*>(env->GetDirectBufferAddress(inL));
    auto* pInR  = static_cast<float*>(env->GetDirectBufferAddress(inR));
    auto* pOutL = static_cast<float*>(env->GetDirectBufferAddress(outL));
    auto* pOutR = static_cast<float*>(env->GetDirectBufferAddress(outR));
    if (!pInL || !pInR || !pOutL || !pOutR) return;
    ivanna::audio::enableAudioThreadFastMathOnce();
    eng->processStereo(pInL, pInR, pOutL, pOutR, numFrames);
}

// ── Parameters ───────────────────────────────────────────────────────────────
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetParameters(
    JNIEnv*, jclass, jlong handle,
    jfloat alpha, jfloat beta, jfloat gamma, jfloat delta,
    jfloat eta, jfloat zeta,
    jfloat srNoiseFloor, jfloat syncThreshold, jfloat noiseGainFar,
    jfloat phi, jfloat damping, jfloat nonlinearity, jfloat coupling) {
    if (Engine* eng = handle_to_engine(handle)) {
        eng->setParameters(alpha, beta, gamma, delta, eta, zeta,
                            srNoiseFloor, syncThreshold, noiseGainFar,
                            phi, damping, nonlinearity, coupling);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetAGC(
    JNIEnv*, jclass, jlong handle, jfloat target, jfloat rate) {
    if (Engine* eng = handle_to_engine(handle)) eng->setAGC(target, rate);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetBypass(
    JNIEnv*, jclass, jlong handle, jboolean bypass) {
    if (Engine* eng = handle_to_engine(handle)) eng->setBypass(bypass == JNI_TRUE);
}

// ── Metrics ───────────────────────────────────────────────────────────────────
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetMetrics(
    JNIEnv* env, jclass, jlong handle) {
    Engine* eng = handle_to_engine(handle);
    if (!eng) return nullptr;
    const auto m = eng->getMetrics();
    jfloatArray arr = env->NewFloatArray(8);
    if (arr) env->SetFloatArrayRegion(arr, 0, 8, m.data());
    return arr;
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSnapshotScope(
    JNIEnv* env, jclass, jlong handle, jobject dst, jint maxFrames) {
    Engine* eng = handle_to_engine(handle);
    if (!eng) return 0;
    auto* pDst = static_cast<float*>(env->GetDirectBufferAddress(dst));
    if (!pDst) return 0;
    return eng->snapshotScope(pDst, maxFrames);
}

// ── Engine flags / neuro params ───────────────────────────────────────────────
JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetEngineFlags(
    JNIEnv*, jclass, jlong handle, jboolean hrtf, jboolean cochlear, jboolean adapt) {
    if (Engine* eng = handle_to_engine(handle)) {
        eng->setEngineFlags(hrtf == JNI_TRUE, cochlear == JNI_TRUE, adapt == JNI_TRUE);
    }
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetNeuroParams(
    JNIEnv*, jclass, jlong handle,
    jfloat harmonicGain, jfloat lateralInhib, jfloat ohcCompression, jfloat masterGainDb) {
    if (Engine* eng = handle_to_engine(handle)) {
        eng->setNeuroParams(harmonicGain, lateralInhib, ohcCompression, masterGainDb);
    }
}

// ── Synth / genre analysis (global — sin handle) ──────────────────────────────
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetDetectedGenre(
    JNIEnv* env, jclass) {
    Engine* eng = g_active.load(std::memory_order_acquire);
    return env->NewStringUTF(eng ? eng->getDetectedGenre() : "\xe2\x80\x94");
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetSynthSignature(
    JNIEnv* env, jclass) {
    jfloatArray arr = env->NewFloatArray(5);
    if (!arr) return arr;
    Engine* eng = g_active.load(std::memory_order_acquire);
    float sig[5] = {0.f, 0.f, 0.f, 0.f, 0.f};
    if (eng) eng->getSynthSignature(sig);
    env->SetFloatArrayRegion(arr, 0, 5, sig);
    return arr;
}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetSynthClassify(
    JNIEnv* env, jclass) {
    jfloatArray arr = env->NewFloatArray(7);
    if (!arr) return arr;
    Engine* eng = g_active.load(std::memory_order_acquire);
    float cls[7] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    if (eng) eng->getSynthClassify(cls);
    env->SetFloatArrayRegion(arr, 0, 7, cls);
    return arr;
}

// ── Build info ────────────────────────────────────────────────────────────────
JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetCopyright(
    JNIEnv* env, jclass) {
    return env->NewStringUTF("© 2026 Luis Uriel Pimentel Pérez — GORE TNS");
}

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetBuildTag(
    JNIEnv* env, jclass) {
    return env->NewStringUTF("IVANNA-NPE-v2.0-NHO-LIF-BEB");
}

// ── Hexagon DSP API — no implementado en esta build (CPU-only) ───────────────
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspOpen(
    JNIEnv*, jclass, jint, jint, jint) { return JNI_FALSE; }

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspClose(
    JNIEnv*, jclass) {}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspIsAvailable(
    JNIEnv*, jclass) { return JNI_FALSE; }

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspSetActive(
    JNIEnv*, jclass, jboolean) {}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspSetNeuroParams(
    JNIEnv*, jclass, jfloat, jfloat, jfloat, jfloat, jfloat,
    jfloat, jfloat, jfloat) {}

JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDspGetMetrics(
    JNIEnv*, jclass) { return nullptr; }

} // extern "C"
