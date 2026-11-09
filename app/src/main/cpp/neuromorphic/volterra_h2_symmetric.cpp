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

// ── Producto punto H1 sobre la línea de retardo circular (v2.5.369) ────────
// y = sum_{k=0}^{K-1} h1[k] * x[n-k], con x[n-k] = delay[(write_idx-k+K)%K].
// La línea es circular: el historial se parte en dos segmentos contiguos
// (sin módulo por tap):
//   Segmento A: k = 0..write_idx      -> delay[write_idx .. 0] (descendente)
//   Segmento B: k = write_idx+1..K-1  -> delay[K-1 .. write_idx+1] (desc.)
// En ARM se vectoriza con NEON (bloques de 4, reducción horizontal); en
// host x86 cae al escalar sin módulos. Misma matemática en ambos paths
// (la prueba de paridad optimizado-vs-escalar lo verifica).
#if defined(__arm64__) || defined(__aarch64__) || defined(__ARM_NEON)
namespace {
inline float32x4_t reverse4Neon(float32x4_t v) noexcept {
    // v = {a,b,c,d} -> {d,c,b,a}: vrev64 invierte cada mitad de 64 bits
    // ({b,a,d,c}); vext rota 2 carriles ({d,c,b,a}).
    const float32x4_t r = vrev64q_f32(v);
    return vextq_f32(r, r, 2);
}
}  // namespace

inline float dotH1Neon(const float* h1, const float* delay, uint32_t K,
                       uint32_t write_idx) noexcept {
    const uint32_t w = write_idx;
    float32x4_t acc = vdupq_n_f32(0.0f);
    // Segmento A: k = 0..w. Bloque de 4 taps en k0: h1[k0..k0+3] directo;
    // delay[(w-k0) .. (w-k0-3)] descendente -> carga ascendente y revierte.
    uint32_t k = 0;
    for (; k + 3 <= w; k += 4) {
        const uint32_t base = w - k;  // delay[base] = x[n-k]
        float32x4_t v_x = vld1q_f32(&delay[base - 3]);
        v_x = reverse4Neon(v_x);
        const float32x4_t v_h = vld1q_f32(&h1[k]);
        acc = vmlaq_f32(acc, v_h, v_x);
    }
    float y = vaddvq_f32(acc);
    for (; k <= w; ++k) y += h1[k] * delay[w - k];
    // Segmento B: k = w+1..K-1. delay[(w-k+K) .. (w-k-3+K)] descendente.
    uint32_t k2 = w + 1;
    float32x4_t acc2 = vdupq_n_f32(0.0f);
    for (; k2 + 3 < K; k2 += 4) {
        const uint32_t base = w - k2 + K;  // delay[base] = x[n-k2]
        float32x4_t v_x = vld1q_f32(&delay[base - 3]);
        v_x = reverse4Neon(v_x);
        const float32x4_t v_h = vld1q_f32(&h1[k2]);
        acc2 = vmlaq_f32(acc2, v_h, v_x);
    }
    y += vaddvq_f32(acc2);
    for (; k2 < K; ++k2) y += h1[k2] * delay[w - k2 + K];
    return y;
}
#endif  // NEON

inline float dotH1Scalar(const float* h1, const float* delay, uint32_t K,
                         uint32_t write_idx) noexcept {
    // Dos segmentos contiguos, sin módulo por tap (v2.5.369).
    float y = 0.0f;
    const uint32_t w = write_idx;
    uint32_t d = w;
    for (uint32_t k = 0; k <= w; ++k) y += h1[k] * delay[d--];
    d = K - 1;
    for (uint32_t k = w + 1; k < K; ++k) y += h1[k] * delay[d--];
    return y;
}

inline float dotProductH1(const float* h1, const float* delay, uint32_t K,
                          uint32_t write_idx) noexcept {
#if defined(__arm64__) || defined(__aarch64__) || defined(__ARM_NEON)
    return dotH1Neon(h1, delay, K, write_idx);
#else
    return dotH1Scalar(h1, delay, K, write_idx);
#endif
}

VolterraH2Symmetric::VolterraH2Symmetric(uint32_t kernel_length, uint32_t channels)
    : m_kernel_length(kernel_length), m_channels(channels) {

    const size_t align = 64;
    if (m_kernel_length == 0) m_kernel_length = 64;
    if (m_kernel_length > 16384) m_kernel_length = 16384;
    if (m_channels == 0) m_channels = 2;
    if (m_channels > 16) m_channels = 16;

    m_transition_env.configure(48000.0f, 8.0f, 18.0f, 35.0f);
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
    free(m_h2_sparse);
    m_delay_lines = nullptr; m_delay_indices = nullptr; m_h1 = nullptr; m_h2 = nullptr;
    m_h2_sparse = nullptr; m_h2_sparse_count = 0;
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
    // Lista dispersa H2 (v2.5.369): solo los coeficientes no nulos del
    // triangular superior. Se reconstruye en cada carga; el hot path
    // itera únicamente esta lista. Coste de construcción O(K^2) en el
    // hilo de CONTROL (nunca en audio); el hot path queda en O(N_nz).
    free(m_h2_sparse);
    m_h2_sparse = nullptr;
    m_h2_sparse_count = 0;
    if (h2_active && m_h2) {
        uint32_t nz = 0;
        for (size_t k = 0; k < h1_size; ++k) {
            for (size_t l = k; l < h1_size; ++l) {
                const size_t idx = k * h1_size + l - (k * (k + 1)) / 2;
                if (m_h2[idx] != 0.0f) ++nz;
            }
        }
        if (nz > 0) {
            m_h2_sparse = static_cast<H2SparseEntry*>(
                memalign(64, static_cast<size_t>(nz) * sizeof(H2SparseEntry)));
            if (!m_h2_sparse) {
                m_kernels_ready.store(true, std::memory_order_seq_cst);
                return S::AllocFailed;
            }
            uint32_t w = 0;
            for (size_t k = 0; k < h1_size; ++k) {
                for (size_t l = k; l < h1_size; ++l) {
                    const size_t idx = k * h1_size + l - (k * (k + 1)) / 2;
                    const float v = m_h2[idx];
                    if (v != 0.0f) {
                        m_h2_sparse[w].k = static_cast<uint16_t>(k);
                        m_h2_sparse[w].l = static_cast<uint16_t>(l);
                        m_h2_sparse[w].v = v;
                        ++w;
                    }
                }
            }
            m_h2_sparse_count = w;
        }
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

            // --- PROCESAMIENTO DEL NÚCLEO LINEAL H1 (v2.5.369) ---
            // write_idx apunta a x[n] (k = 0). dotProductH1 usa los dos
            // segmentos contiguos de la línea circular (sin módulo por
            // tap); NEON en ARM, escalar en host. Misma matemática.
            float y_linear = x;
            if (!m_h1_identity) {
                y_linear = dotProductH1(m_h1, delay, K, write_idx);
            }

            // --- PROCESAMIENTO DEL NÚCLEO CUADRÁTICO H2 DISPERSO (v2.5.369) ---
            // Solo itera los coeficientes H2 no nulos (lista construida en
            // applyKernels). Para el PZ1D son 16 MACs por muestra, no 2080.
            // El salto por umbral (|x_k| < 1e-6, ~-120 dBFS) se conserva:
            // omite términos despreciables, como hacía el bucle denso.
            float y_quad = 0.0f;
            if (m_h2_active && m_h2_sparse) {
                for (uint32_t s = 0; s < m_h2_sparse_count; ++s) {
                    const uint32_t k = m_h2_sparse[s].k;
                    const uint32_t l = m_h2_sparse[s].l;
                    const float v = m_h2_sparse[s].v;
                    const float x_k = delay[(write_idx + K - k) % K];
                    if (std::abs(x_k) < 1e-6f) continue;
                    const float x_l = delay[(write_idx + K - l) % K];
                    if (x_l == 0.0f) continue;
                    const float mult = (k == l) ? 1.0f : 2.0f;
                    y_quad += mult * v * x_k * x_l;
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
