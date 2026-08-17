// ============================================================================
//  ivanna_npe_engine.cpp — Fase H (NPE Engine real, sin duplicar FastRPC)
//  © 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.
//
//  El archivo previo (mismo path) duplicaba TODA la clase
//  IvannaFastRpcClient — el mismo símbolo que ya define
//  hexagon/ivanna_fastrpc_client.cpp. Ese fichero además no estaba listado
//  en CMakeLists.txt, así que quedaba fuera del build; si alguien lo
//  añadía provocaba "multiple definition" en el linker.
//
//  Esta versión Fase H:
//    - Elimina la duplicación de IvannaFastRpcClient.
//    - Conserva el generador de coeficientes FIR (Blackman-Harris x sinc)
//      y el fallback CPU FIRUpsamplerEngine (48kHz→768kHz, 1024 taps).
//    - Ofrece una fachada `NpeEngine` estable, con selección automática
//      DSP vs CPU basada en `ivanna::hexagon::is_available()`.
//    - Sin excepciones, sin RTTI, lock-free en la ruta caliente, buffers
//      alineados a 64 bytes para HVX / NEON.
//
//  Añadir a CMakeLists.txt (sección add_library(ivanna_omega …)):
//      neuromorphic/ivanna_npe_engine.cpp
// ============================================================================

#include "../hexagon/hexagon_dsp_integration.hpp"
#include "../hexagon/ivanna_fastrpc_client.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <malloc.h>
#include <memory>
#include <mutex>

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

namespace ivanna { namespace npe {

// ── Constantes del filtro ────────────────────────────────────────────────────
static constexpr uint32_t FIR_TAPS         = 1024;   // ~0.78 GMACs @ 768kHz
static constexpr uint32_t UPSAMPLE_FACTOR  = 16;     // 48kHz → 768kHz
static constexpr uint32_t HVX_ALIGN_BYTES  = 64;

// ── Coeficientes FIR (thread-safe, inicialización única) ─────────────────────
struct alignas(HVX_ALIGN_BYTES) FIRCoefficients { float data[FIR_TAPS]; };
static FIRCoefficients            g_fir_coefficients_storage;
static std::once_flag             g_fir_coefficients_once;

static inline float blackmanHarris(int n, int N) noexcept {
    constexpr float a0 = 0.35875f, a1 = 0.48829f;
    constexpr float a2 = 0.14128f, a3 = 0.01168f;
    const float x = (2.0f * static_cast<float>(M_PI) * n) / (N - 1);
    return a0 - a1 * cosf(x) + a2 * cosf(2.0f * x) - a3 * cosf(3.0f * x);
}

static void generateFIRCoefficients() noexcept {
    std::call_once(g_fir_coefficients_once, [] {
        const float cutoff = 1.0f / (2.0f * UPSAMPLE_FACTOR);
        for (uint32_t i = 0; i < FIR_TAPS; ++i) {
            const int32_t n = static_cast<int32_t>(i) -
                              static_cast<int32_t>(FIR_TAPS / 2);
            const float sinc = (n == 0) ? 1.0f
                : sinf(static_cast<float>(M_PI) * cutoff * n) /
                  (static_cast<float>(M_PI) * cutoff * n);
            const float w = blackmanHarris(static_cast<int>(i), FIR_TAPS);
            g_fir_coefficients_storage.data[i] = sinc * w * cutoff * 2.0f;
        }
    });
}

// ── Fallback CPU (FIR polifásico directo) ────────────────────────────────────
class FIRUpsamplerCPU {
public:
    FIRUpsamplerCPU() noexcept
        : m_delay_line(nullptr), m_delay_index(0) {
        generateFIRCoefficients();
        m_delay_line = static_cast<float*>(
            memalign(HVX_ALIGN_BYTES, FIR_TAPS * sizeof(float)));
        if (m_delay_line) std::memset(m_delay_line, 0, FIR_TAPS * sizeof(float));
    }

    ~FIRUpsamplerCPU() {
        if (m_delay_line) free(m_delay_line);
    }

    FIRUpsamplerCPU(const FIRUpsamplerCPU&)            = delete;
    FIRUpsamplerCPU& operator=(const FIRUpsamplerCPU&) = delete;

    // input: input_frames muestras @ Fs
    // output: input_frames * UPSAMPLE_FACTOR muestras @ Fs*16
    // Retorna false si buffers/estado inválidos.
    bool process(const float* input, float* output, uint32_t input_frames) noexcept {
        if (!input || !output || input_frames == 0 || !m_delay_line) return false;
        const float* coeff = g_fir_coefficients_storage.data;

        for (uint32_t n = 0; n < input_frames; ++n) {
            m_delay_line[m_delay_index] = input[n];
            m_delay_index = (m_delay_index + 1) % FIR_TAPS;

            for (uint32_t phase = 0; phase < UPSAMPLE_FACTOR; ++phase) {
                float acc = 0.0f;
                uint32_t tap = phase;
                const uint32_t d0 = m_delay_index;
                while (tap < FIR_TAPS) {
                    const uint32_t tap_div = tap / UPSAMPLE_FACTOR;
                    if (tap_div >= FIR_TAPS) break;
                    const uint32_t d = (d0 + FIR_TAPS - tap_div - 1) % FIR_TAPS;
                    acc += m_delay_line[d] * coeff[tap];
                    tap += UPSAMPLE_FACTOR;
                }
                output[n * UPSAMPLE_FACTOR + phase] = acc;
            }
        }
        return true;
    }

    void reset() noexcept {
        if (m_delay_line) std::memset(m_delay_line, 0, FIR_TAPS * sizeof(float));
        m_delay_index = 0;
    }

private:
    float*   m_delay_line;
    uint32_t m_delay_index;
};

// ── Fachada pública ──────────────────────────────────────────────────────────
class NpeEngine {
public:
    enum class Backend : uint8_t { UNINITIALIZED, HEXAGON_DSP, CPU_FALLBACK };

    NpeEngine() noexcept : m_backend(Backend::UNINITIALIZED) {}

    bool initialize(uint32_t sample_rate_in  = 48000,
                    uint32_t sample_rate_out = 768000,
                    uint32_t block_size      = 1024) noexcept {
        const uint32_t up = (sample_rate_in > 0)
            ? (sample_rate_out / sample_rate_in) : UPSAMPLE_FACTOR;
        m_upsample_factor = (up == 0) ? UPSAMPLE_FACTOR : up;

        if (ivanna::hexagon::ensure_available()) {
            ivanna::dsp::HrtfConvolutionConfig cfg{};
            cfg.sample_rate_in     = sample_rate_in;
            cfg.sample_rate_out    = sample_rate_out;
            cfg.hrtf_filter_length = FIR_TAPS;
            cfg.block_size         = block_size;
            cfg.num_azimuth_bins   = 360;
            cfg.num_elevation_bins = 180;
            cfg.use_fft_convolution = true;
            if (m_frpc.initialize(cfg)) {
                m_backend.store(Backend::HEXAGON_DSP, std::memory_order_release);
                return true;
            }
        }
        m_cpu = std::make_unique<FIRUpsamplerCPU>();
        m_backend.store(Backend::CPU_FALLBACK, std::memory_order_release);
        return true;
    }

    void teardown() noexcept {
        m_frpc.teardown();
        m_cpu.reset();
        m_backend.store(Backend::UNINITIALIZED, std::memory_order_release);
    }

    Backend backend() const noexcept {
        return m_backend.load(std::memory_order_acquire);
    }

    // Upsampling: input_frames @ Fs → input_frames*factor @ Fs_out.
    bool upsample(const float* input, float* output, uint32_t input_frames) noexcept {
        const Backend b = m_backend.load(std::memory_order_acquire);
        if (b == Backend::HEXAGON_DSP) {
            if (m_frpc.delegateFIRUpsampling(input, output,
                    input_frames, input_frames * m_upsample_factor)) {
                return true;
            }
            // DSP falló en runtime: degradar a CPU sin perder audio.
            if (!m_cpu) m_cpu = std::make_unique<FIRUpsamplerCPU>();
            m_backend.store(Backend::CPU_FALLBACK, std::memory_order_release);
        }
        if (!m_cpu) m_cpu = std::make_unique<FIRUpsamplerCPU>();
        return m_cpu->process(input, output, input_frames);
    }

    void reset() noexcept {
        if (m_cpu) m_cpu->reset();
    }

private:
    std::atomic<Backend>              m_backend;
    ivanna::dsp::IvannaFastRpcClient  m_frpc;
    std::unique_ptr<FIRUpsamplerCPU>  m_cpu;
    uint32_t                          m_upsample_factor = UPSAMPLE_FACTOR;

    NpeEngine(const NpeEngine&)            = delete;
    NpeEngine& operator=(const NpeEngine&) = delete;
};

// Instancia global opcional (patrón singleton perezoso). El JNI la usa vía
// npe_engine_instance(); código nuevo puede construir su propia NpeEngine.
NpeEngine& npe_engine_instance() noexcept {
    static NpeEngine s;
    return s;
}

}} // namespace ivanna::npe
