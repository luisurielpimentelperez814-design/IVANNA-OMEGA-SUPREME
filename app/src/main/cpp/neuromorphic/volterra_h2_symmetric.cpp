/*
 * ============================================================================
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 * Luis Uriel Pimentel Pérez (alias Gore TNS)
 *
 * Todos los modelos matemáticos, arquitecturas de sistema e implementaciones
 * de código contenidos en este archivo son propiedad intelectual exclusiva
 * del autor citado. Queda estrictamente prohibida la reproducción, distribución,
 * modificación o uso comercial no autorizado.
 *
 * Este software NO se distribuye bajo licencia CC0 ni dominio público.
 * Todos los derechos reservados. © 2026 Luis Uriel Pimentel Pérez.
 * ============================================================================
 */

#include "../include/volterra_h2_symmetric.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <malloc.h>
#include <algorithm>

// Inclusión obligatoria de las intrínsecas vectoriales por hardware de ARM
#if defined(__arm64__) || defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace ivanna {
namespace dsp {

VolterraH2Symmetric::VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels,
                                           uint32_t sample_rate)
    : m_kernel_length(kernel_length), m_channels(channels) {

    const size_t align = 64;
    if (m_kernel_length == 0) m_kernel_length = 64;
    if (m_kernel_length > 16384) m_kernel_length = 16384;
    if (m_channels == 0) m_channels = 2;
    if (m_channels > 16) m_channels = 16;

    const uint32_t initialSr = (sample_rate >= 8000u && sample_rate <= 768000u)
        ? sample_rate : 48000u;
    m_transition_env.configure(static_cast<float>(initialSr), 8.0f, 18.0f, 35.0f);
    m_transition_env.setImmediate(0.0f);

    m_h1 = static_cast<float*>(memalign(align, static_cast<size_t>(m_kernel_length) * sizeof(float)));
    // H2 es cuadrático O(K²): no reservar cientos de MB para el kernel cero
    // que acompaña al estado sin calibrar. Se materializa solo al cargar H2 real.
    m_h2 = nullptr;
    m_delay_lines = static_cast<float**>(calloc(m_channels, sizeof(float*)));
    m_delay_indices = static_cast<uint32_t*>(calloc(m_channels, sizeof(uint32_t)));
    if (!m_h1 || !m_delay_lines || !m_delay_indices) {
        releaseStorage(); m_channels = 0; return;
    }
    memset(m_h1, 0, static_cast<size_t>(m_kernel_length) * sizeof(float));
    m_h1[0] = 1.0f;
    for (uint32_t ch = 0; ch < m_channels; ++ch) {
        m_delay_lines[ch] = static_cast<float*>(memalign(
            align, static_cast<size_t>(m_kernel_length) * sizeof(float)));
        if (!m_delay_lines[ch]) {
            releaseStorage(); m_channels = 0; return;
        }
        memset(m_delay_lines[ch], 0, static_cast<size_t>(m_kernel_length) * sizeof(float));
    }
    m_h1_identity = true;
    m_h2_active = false;
    m_effective_kernel.store(false, std::memory_order_release);
    m_kernels_ready.store(true, std::memory_order_release);
}

void VolterraH2Symmetric::releaseStorage() noexcept {
    if (m_delay_lines) for (uint32_t ch = 0; ch < m_channels; ++ch) free(m_delay_lines[ch]);
    free(m_delay_lines); free(m_delay_indices); free(m_h1); free(m_h2);
    m_delay_lines = nullptr; m_delay_indices = nullptr; m_h1 = nullptr; m_h2 = nullptr;
}

VolterraH2Symmetric::~VolterraH2Symmetric() { releaseStorage(); }

void VolterraH2Symmetric::updateKernels(
    const float* h1_kernel,
    const float* h2_kernel,
    uint32_t length
) noexcept {
    // Semántica legacy intacta: rechazo silencioso. Ver updateKernelsChecked().
    (void)updateKernelsChecked(h1_kernel, h2_kernel, length);
}

VolterraLoadStatus VolterraH2Symmetric::updateKernelsChecked(
    const float* h1_kernel,
    const float* h2_kernel,
    uint32_t length
) noexcept {
    if (!h1_kernel || !h2_kernel) return VolterraLoadStatus::NullArgument;
    return applyKernels(h1_kernel, h2_kernel, length);
}

VolterraLoadStatus VolterraH2Symmetric::applyKernels(
    const float* h1_kernel,
    const float* h2_kernel,
    uint32_t length
) noexcept {
    using S = VolterraLoadStatus;
    if (!h1_kernel || !m_h1 || m_channels == 0) return S::EngineUnavailable;
    if (length != m_kernel_length) return S::KernelLengthMismatch;
    if (isEnabled()) return S::EngineBusy;
    const size_t h1_size = static_cast<size_t>(length);
    const size_t h2_size = (h1_size * (h1_size + 1u)) / 2u;
    bool h1_identity = h1_kernel[0] == 1.0f;
    bool h2_active = false;
    for (size_t i = 0; i < h1_size; ++i) {
        if (!std::isfinite(h1_kernel[i])) return S::NonFinite;
        if (i != 0 && h1_kernel[i] != 0.0f) h1_identity = false;
    }
    if (h2_kernel) {
        for (size_t i = 0; i < h2_size; ++i) {
            if (!std::isfinite(h2_kernel[i])) return S::NonFinite;
            if (h2_kernel[i] != 0.0f) h2_active = true;
        }
    }
    if (!m_kernels_ready.exchange(false, std::memory_order_seq_cst)) return S::EngineBusy;
    if (m_active_processes.load(std::memory_order_seq_cst) != 0u ||
        !m_dsp_silent.load(std::memory_order_acquire)) {
        m_kernels_ready.store(true, std::memory_order_seq_cst);
        return S::EngineBusy;
    }
    if (h2_active && !m_h2) {
        m_h2 = static_cast<float*>(memalign(64, h2_size * sizeof(float)));
        if (!m_h2) {
            m_kernels_ready.store(true, std::memory_order_seq_cst);
            return S::AllocFailed;
        }
    }
    memcpy(m_h1, h1_kernel, h1_size * sizeof(float));
    if (h2_active) {
        memcpy(m_h2, h2_kernel, h2_size * sizeof(float));
    } else if (m_h2) {
        free(m_h2);
        m_h2 = nullptr;
    }
    m_h1_identity = h1_identity;
    m_h2_active = h2_active;
    m_effective_kernel.store(!h1_identity || h2_active, std::memory_order_release);
    m_kernels_ready.store(true, std::memory_order_seq_cst);
    return S::Ok;
}

VolterraLoadStatus VolterraH2Symmetric::loadKernelPackage(
    const uint8_t* package,
    size_t package_size,
    const VolterraLoadPolicy& policy
) noexcept {
    using S = VolterraLoadStatus;
    VolterraLoadPolicy pol = policy;
    pol.expected_kernel_length = m_kernel_length;
    VolterraPackageView view{};
    S st = volterraValidatePackage(package, package_size, pol, &view);
    if (st == S::Ok) {
        const size_t K = view.kernel_length;
        const size_t h2n = view.h2_bytes ? (K * (K + 1u)) / 2u : 0u;
        const bool h1Aligned = (reinterpret_cast<uintptr_t>(view.h1_bytes) % alignof(float)) == 0u;
        const bool h2Aligned = !view.h2_bytes ||
            (reinterpret_cast<uintptr_t>(view.h2_bytes) % alignof(float)) == 0u;
        float* tmp1 = nullptr;
        float* tmp2 = nullptr;
        const float* p1 = reinterpret_cast<const float*>(view.h1_bytes);
        const float* p2 = reinterpret_cast<const float*>(view.h2_bytes);
        if (!h1Aligned) {  // hilo de control: copia alineada (sin UB por acceso no alineado)
            tmp1 = static_cast<float*>(malloc(K * sizeof(float)));
            if (!tmp1) st = S::AllocFailed; else { memcpy(tmp1, view.h1_bytes, K * sizeof(float)); p1 = tmp1; }
        }
        if (st == S::Ok && !h2Aligned) {
            tmp2 = static_cast<float*>(malloc(h2n * sizeof(float)));
            if (!tmp2) st = S::AllocFailed; else { memcpy(tmp2, view.h2_bytes, h2n * sizeof(float)); p2 = tmp2; }
        }
        if (st == S::Ok) st = applyKernels(p1, p2, static_cast<uint32_t>(K));
        free(tmp1);
        free(tmp2);
    }
    if (st != S::Ok) setEnabled(false);  // bypass inmediato; kernels previos intactos
    return st;
}

void VolterraH2Symmetric::preserveInterleavedTail(
    const float* input,
    uint32_t num_frames,
    uint32_t num_channels
) noexcept {
    if (!input || num_frames == 0 || !m_delay_lines || !m_delay_indices) return;
    const uint32_t K = m_kernel_length;
    if (K == 0) {
        m_dsp_silent.store(m_transition_env.isSilent(), std::memory_order_release);
        m_published_gain.store(m_transition_env.currentGain, std::memory_order_release);
        return;
    }
    const uint32_t chCount = std::min(num_channels, m_channels);
    const uint32_t tailFrames = std::min(num_frames, K);
    const uint32_t startFrame = num_frames - tailFrames;
    for (uint32_t n = startFrame; n < num_frames; ++n) {
        for (uint32_t ch = 0; ch < chCount; ++ch) {
            float* delay = m_delay_lines[ch];
            if (!delay) continue;
            uint32_t& d_idx = m_delay_indices[ch];
            float x = input[n * num_channels + ch];
            if (!std::isfinite(x) || std::abs(x) < 1e-30f) x = 0.0f;
            delay[d_idx] = x;
            d_idx = (d_idx + 1) % K;
        }
    }
    m_continuity_mgr.preserveState();
}

void VolterraH2Symmetric::processInterleaved(
    const float* input,
    float* output,
    uint32_t num_frames,
    uint32_t num_channels
) noexcept {
    if (!input || !output) return;
    m_active_processes.fetch_add(1u, std::memory_order_seq_cst);
    struct ActiveProcessGuard {
        std::atomic<uint32_t>& count;
        ~ActiveProcessGuard() { count.fetch_sub(1u, std::memory_order_seq_cst); }
    } activeGuard{m_active_processes};

    if (!m_kernels_ready.load(std::memory_order_seq_cst)) {
        if (output != input) {
            memcpy(output, input, static_cast<size_t>(num_frames) * num_channels * sizeof(float));
        }
        return;
    }
    // Apply a route-rate change only on the DSP thread. Re-anchor the envelope
    // at its current gain so the next beginBlock() rebuilds the remaining ramp
    // using the new sample clock without a discontinuity.
    const uint32_t requestedSr = m_pending_sample_rate.exchange(0u, std::memory_order_acq_rel);
    if (requestedSr >= 8000u && requestedSr <= 768000u &&
        static_cast<uint32_t>(m_transition_env.sampleRate) != requestedSr) {
        const float gain = m_transition_env.currentGain;
        m_transition_env.configure(static_cast<float>(requestedSr), 8.0f, 18.0f, 35.0f);
        m_transition_env.currentGain = gain;
        m_transition_env.targetGain = gain;
        m_transition_env.step = 0.0f;
        m_transition_env.remainingSamples = 0u;
    }

    if (!m_effective_kernel.load(std::memory_order_acquire)) {
        // Mantener caliente el historial circular aun con H1 identidad/H2 cero.
        // Una calibración posterior puede entrar sin arrancar con memoria vacía.
        preserveInterleavedTail(input, num_frames, num_channels);
        if (output != input) {
            memcpy(output, input, static_cast<size_t>(num_frames) * num_channels * sizeof(float));
        }
        return;
    }

    const bool wantEnabled = m_enabled.load(std::memory_order_acquire);
    const bool thermal     = m_thermal_bypass.load(std::memory_order_relaxed);
    const float targetGain = (wantEnabled && !thermal) ? 1.0f : 0.0f;
    const auto profile     = thermal
        ? ivanna::supreme::TransitionProfile::Thermal
        : ivanna::supreme::TransitionProfile::Standard;
    const bool wasSilent   = m_transition_env.isSilent();

    if (!m_transition_env.beginBlock(targetGain, profile)) {
        m_dsp_silent.store(true, std::memory_order_release);
        m_published_gain.store(0.0f, std::memory_order_release);
        preserveInterleavedTail(input, num_frames, num_channels);
        m_continuity_mgr.suspend();
        if (output != input) {
            memcpy(output, input, num_frames * num_channels * sizeof(float));
        }
        return;
    }
    if (wasSilent) {
        m_continuity_mgr.resume();
    }

    if (num_channels == 0) {
        m_dsp_silent.store(m_transition_env.isSilent(), std::memory_order_release);
        m_published_gain.store(m_transition_env.currentGain, std::memory_order_release);
        return;
    }
    const uint32_t input_channels = num_channels;
    const uint32_t process_channels = std::min(num_channels, m_channels);

    // CAPTURA LOCAL SEGURA: Blindaje contra Race Conditions que provocan el desbordamiento de búfer (SIGSEGV)
    const uint32_t K = m_kernel_length;
    if (K == 0) return;
    const size_t h2_max_size = (K * (K + 1)) / 2;

    // 1. ESCUDO DE HARDWARE: Forzar modo Flush-to-Zero (FTZ) en la arquitectura ARM de Android
    // Evita que la CPU degrade su rendimiento procesando números infinitesimales (Elimina el desvanecimiento)
#if defined(__arm64__) || defined(__aarch64__)
    #if defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1 << 24);
    asm volatile("msr fpcr, %0" :: "r"(fpcr));
#elif defined(__arm__)
    uint32_t fpscr;
    asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24);
    asm volatile("vmsr fpscr, %0" :: "r"(fpscr));
#endif
#endif

    for (uint32_t n = 0; n < num_frames; ++n) {
        const float env = m_transition_env.nextSample();
        for (uint32_t ch = 0; ch < input_channels; ++ch) {
            const uint32_t idx = n * input_channels + ch;
            if (ch >= process_channels) { output[idx] = input[idx]; continue; }
            const float dry = input[idx];
            float x = dry;

            // Compuerta de ruido rápida contra el fango digital residual en el flujo
            if (std::abs(x) < 1e-30f) x = 0.0f;

            if (!m_delay_lines[ch]) {
                output[idx] = x;
                continue;
            }

            float* delay = m_delay_lines[ch];
            uint32_t& d_idx = m_delay_indices[ch];

            const uint32_t write_idx = d_idx;
            delay[write_idx] = x;
            d_idx = (write_idx + 1) % K;

            // --- PROCESAMIENTO DEL NÚCLEO LINEAL H1 ---
            // write_idx apunta a x[n] (k = 0); (write_idx + K - k) % K apunta a x[n - k].
            float y_linear = x;
            if (!m_h1_identity) {
                y_linear = 0.0f;
                for (uint32_t k = 0; k < K; ++k) {
                    const uint32_t delay_k = (write_idx + K - k) % K;
                    y_linear += m_h1[k] * delay[delay_k];
                }
            }

            // --- PROCESAMIENTO DEL NÚCLEO CUADRÁTICO H2 (ARM NEON VECTORIZADO) ---
            float y_quad = 0.0f;

            for (uint32_t k = 0; m_h2_active && k < K; ++k) {
                uint32_t delay_k = (write_idx + K - k) % K;
                float x_k = delay[delay_k];
                
                // Umbral psicoacústico inteligente (~-120 dBFS). Si la muestra es silencio, saltamos la fila completa
                if (std::abs(x_k) < 1e-6f) continue; 

                uint32_t l = k;

#if defined(__arm64__) || defined(__aarch64__) || defined(__ARM_NEON)
                // Ejecución SIMD de alto rendimiento: Procesamos bloques paralelos de 4 flotantes
                for (; l + 3 < K; l += 4) {
                    uint32_t h2_idx0 = k * K + l - (k * (k + 1)) / 2;
                    
                    // Doble validación de memoria estricta para mitigar caídas de punteros corruptos
                    if (h2_idx0 + 3 >= h2_max_size) break;

                    // Cargar coeficientes H2 simétricos desde memoria alineada
                    float32x4_t v_h2 = vld1q_f32(&m_h2[h2_idx0]);

                    // Resolver punteros del buffer circular para los próximos 4 elementos de retraso
                    uint32_t delay_l0 = (write_idx + K - l) % K;
                    uint32_t delay_l1 = (write_idx + K - (l + 1)) % K;
                    uint32_t delay_l2 = (write_idx + K - (l + 2)) % K;
                    uint32_t delay_l3 = (write_idx + K - (l + 3)) % K;

                    alignas(16) float samples_l[4] = {
                        delay[delay_l0],
                        delay[delay_l1],
                        delay[delay_l2],
                        delay[delay_l3]
                    };
                    float32x4_t v_xl = vld1q_f32(samples_l);

                    // Operación Fused Multiply: v_prod = v_h2 * x_k * v_xl
                    float32x4_t v_xk = vmovq_n_f32(x_k);
                    float32x4_t v_prod = vmulq_f32(v_h2, v_xk);
                    v_prod = vmulq_f32(v_prod, v_xl);

                    // Ajustar el factor escalar multiplicador (1.0f si k == l, de lo contrario 2.0f por simetría)
                    alignas(16) float factors[4] = { (k == l) ? 1.0f : 2.0f, 2.0f, 2.0f, 2.0f };
                    float32x4_t v_fac = vld1q_f32(factors);
                    v_prod = vmulq_f32(v_prod, v_fac);

                    // Acumulador escalar rápido desde los carriles NEON
                    y_quad += vgetq_lane_f32(v_prod, 0);
                    y_quad += vgetq_lane_f32(v_prod, 1);
                    y_quad += vgetq_lane_f32(v_prod, 2);
                    y_quad += vgetq_lane_f32(v_prod, 3);
                }
#endif

                // Bucle de limpieza matemático para procesar muestras remanentes fuera del bloque de 4
                for (; l < K; ++l) {
                    uint32_t delay_l = (write_idx + K - l) % K;
                    float x_l = delay[delay_l];
                    if (x_l == 0.0f) continue;

                    uint32_t h2_idx = k * K + l - (k * (k + 1)) / 2;
                    if (h2_idx >= h2_max_size) continue;

                    float h2_val = m_h2[h2_idx];
                    float multiplier = (k == l) ? 1.0f : 2.0f;
                    
                    y_quad += multiplier * h2_val * x_k * x_l;
                }
            }

            float y = y_linear + y_quad;
            if (!std::isfinite(y)) y = dry;

            // Saturador racional C1 (sin recorte duro ni filtro peine parásito)
            const float ay = std::abs(y);
            if (ay > 0.90f) {
                constexpr float kKnee = 0.90f;
                constexpr float kCeil = 0.995f;
                constexpr float kRange = kCeil - kKnee;
                const float over = (ay - kKnee) / kRange;
                const float mag = kKnee + kRange * (over / (1.0f + over));
                y = (y < 0.0f) ? -mag : mag;
            }

            output[idx] = ivanna::supreme::SupremeTransitionEnvelope::mixSample(dry, y, env);
        }
    }

    const bool dspSilent = m_transition_env.isSilent();
    m_dsp_silent.store(dspSilent, std::memory_order_release);
    m_published_gain.store(m_transition_env.currentGain, std::memory_order_release);
    m_continuity_mgr.preserveState();
    if (dspSilent) {
        m_continuity_mgr.suspend();
    }
}

} // namespace dsp
} // namespace ivanna
