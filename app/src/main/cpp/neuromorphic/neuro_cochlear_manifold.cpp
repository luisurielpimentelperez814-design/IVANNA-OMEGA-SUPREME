/*
 * IVANNA Singularity V3.0 — Motor de Audio Holográfico de Bajo Nivel
 * © 2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * FIXES:
 * 1. Fallback HRTF initializes both channels
 * 2. Buffer size validation
 * 3. Proper planar vs interleaved handling
 */

#include "../hexagon/ivanna_fastrpc_client.hpp"
#include "volterra_h2_symmetric.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <malloc.h>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

#include "fir_upsampler_engine.hpp"
#include "neuro_cochlear_manifold.hpp"

namespace ivanna {
namespace dsp {

struct ManifoldState {
    float* buffer_pre_hrtf = nullptr;
    float* buffer_post_hrtf = nullptr;
    float* buffer_post_up = nullptr;
    float* buffer_final = nullptr;

    size_t block_size = 512;
    size_t upsample_factor = 16;
    size_t channels = 2;

    IvannaFastRpcClient* dsp_client = nullptr;
    FIRUpsamplerEngine* upsampler = nullptr;
    VolterraH2Symmetric* volterra = nullptr;

    std::atomic<bool> pipeline_active{false};
};

static ManifoldState g_manifold;

void neuro_cochlear_manifold_teardown();

bool neuro_cochlear_manifold_init(
    uint32_t block_size,
    uint32_t sample_rate_in,
    uint32_t sample_rate_out,
    uint32_t channels
) {
    if (block_size == 0 || sample_rate_in == 0 || sample_rate_out == 0 || channels == 0) {
        return false;
    }
    if (sample_rate_out < sample_rate_in) {
        return false;
    }

    g_manifold.block_size = block_size;
    g_manifold.channels = channels;
    g_manifold.upsample_factor = sample_rate_out / sample_rate_in;

    if (g_manifold.upsample_factor == 0) {
        g_manifold.upsample_factor = 1;
    }

    const size_t align = 64;
    const size_t pre_size = block_size * channels * sizeof(float);
    const size_t post_hrtf_sz = block_size * channels * sizeof(float);
    const size_t up_N = block_size * g_manifold.upsample_factor;
    const size_t post_up_sz = up_N * channels * sizeof(float);

    g_manifold.buffer_pre_hrtf = static_cast<float*>(memalign(align, pre_size));
    g_manifold.buffer_post_hrtf = static_cast<float*>(memalign(align, post_hrtf_sz));
    g_manifold.buffer_post_up = static_cast<float*>(memalign(align, post_up_sz));
    g_manifold.buffer_final = static_cast<float*>(memalign(align, post_up_sz));

    if (!g_manifold.buffer_pre_hrtf || !g_manifold.buffer_post_hrtf ||
        !g_manifold.buffer_post_up || !g_manifold.buffer_final) {
        neuro_cochlear_manifold_teardown();
        return false;
    }

    memset(g_manifold.buffer_pre_hrtf, 0, pre_size);
    memset(g_manifold.buffer_post_hrtf, 0, post_hrtf_sz);
    memset(g_manifold.buffer_post_up, 0, post_up_sz);
    memset(g_manifold.buffer_final, 0, post_up_sz);

    g_manifold.dsp_client = new IvannaFastRpcClient();
    HrtfConvolutionConfig hrtf_cfg;
    hrtf_cfg.sample_rate_in = sample_rate_in;
    hrtf_cfg.sample_rate_out = sample_rate_out;
    hrtf_cfg.hrtf_filter_length = 512;
    hrtf_cfg.block_size = block_size;
    hrtf_cfg.num_azimuth_bins = 360;
    hrtf_cfg.num_elevation_bins = 180;
    hrtf_cfg.use_fft_convolution = true;

    if (!g_manifold.dsp_client->initialize(hrtf_cfg)) {
        delete g_manifold.dsp_client;
        g_manifold.dsp_client = nullptr;
    }

    g_manifold.upsampler = new FIRUpsamplerEngine();
    g_manifold.volterra = new VolterraH2Symmetric(8192, channels);

    g_manifold.pipeline_active.store(true, std::memory_order_release);
    return true;
}

void neuro_cochlear_process_block(
    const float* __restrict__ input_left,
    const float* __restrict__ input_right,
    int32_t* __restrict__ output_s32,
    const SpatialPosition& position
) {
    if (!g_manifold.pipeline_active.load(std::memory_order_acquire)) return;
    if (!input_left || !input_right || !output_s32) return;

    const size_t N = g_manifold.block_size;
    const size_t up_factor = g_manifold.upsample_factor;
    const size_t up_N = N * up_factor;
    const size_t ch = g_manifold.channels;

    if (ch < 2) return;

    // FIX(overflow latente + retorno ignorado):
    //  1) La ruta DSP escribía el canal R en `buffer_post_hrtf + up_N`, pero
    //     ese buffer tiene N*2 floats — con up_factor > 1 desbordaba el heap.
    //     El puntero correcto del canal R es `buffer_post_hrtf + N` (planar).
    //  2) El retorno de delegateBinauralConvolution se ignoraba: si el DSP
    //     falla en runtime (p.ej. sesion FastRPC caida), buffer_post_hrtf
    //     quedaba con basura que seguía por el pipeline como si fuera audio.
    //     Ahora: fallo del DSP => fallback al copy planar en el mismo bloque.
    bool hrtf_ok = false;
    if (g_manifold.dsp_client && g_manifold.dsp_client->isDSPReady()) {
        hrtf_ok = g_manifold.dsp_client->delegateBinauralConvolution(
            input_left, input_right,
            g_manifold.buffer_post_hrtf,        // L planar [0, N)
            g_manifold.buffer_post_hrtf + N,    // R planar [N, 2N)
            position, N);
    }
    if (!hrtf_ok) {
        memcpy(g_manifold.buffer_post_hrtf, input_left, N * sizeof(float));
        memcpy(g_manifold.buffer_post_hrtf + N, input_right, N * sizeof(float));
    }

    if (g_manifold.upsampler) {
        // FIX: antes no se pasaba el factor -> FIRUpsamplerEngine usaba su
        // FACTOR=4 hardcodeado sin importar up_factor real, desbordando
        // buffer_post_up (dimensionado con up_factor, hoy =1). Ver
        // fir_upsampler_engine.hpp.
        // FIX(contaminación cruzada L/R): la 2ª llamada no pasaba ch=1, así
        // que el estado del anti-aliasing (prev_) era compartido: la cola del
        // filtro del canal IZQUIERDO contaminaba el primer sample del canal
        // DERECHO en cada bloque (bleed audible inter-canal a frecuencia de
        // bloque). Ahora cada canal mantiene su propio estado de filtro.
        g_manifold.upsampler->process(
            g_manifold.buffer_post_hrtf, 
            g_manifold.buffer_post_up, 
            N, static_cast<int>(up_factor), /*ch=*/0);
        g_manifold.upsampler->process(
            g_manifold.buffer_post_hrtf + N, 
            g_manifold.buffer_post_up + up_N, 
            N, static_cast<int>(up_factor), /*ch=*/1);
    }

    if (g_manifold.volterra) {
        for (size_t i = 0; i < up_N; ++i) {
            g_manifold.buffer_final[i * 2] = g_manifold.buffer_post_up[i];
            g_manifold.buffer_final[i * 2 + 1] = g_manifold.buffer_post_up[up_N + i];
        }

        g_manifold.volterra->processInterleaved(
            g_manifold.buffer_final,
            g_manifold.buffer_final,
            up_N, ch);
    }

    const size_t total = up_N * ch;
    const float scale = 2147483647.0f;

#ifdef __aarch64__
    float32x4_t vscale = vdupq_n_f32(scale);
    float32x4_t vone = vdupq_n_f32( 1.0f);
    float32x4_t vnone = vdupq_n_f32(-1.0f);

    size_t i = 0;
    size_t blocks = total >> 2;
    for (size_t b = 0; b < blocks; ++b, i += 4) {
        float32x4_t vf = vld1q_f32(g_manifold.buffer_final + i);
        vf = vmaxq_f32(vminq_f32(vf, vone), vnone);
        int32x4_t vi = vcvtq_s32_f32(vmulq_f32(vf, vscale));
        vst1q_s32(output_s32 + i, vi);
    }
    for (; i < total; ++i) {
        float s = g_manifold.buffer_final[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        output_s32[i] = (int32_t)(s * scale);
    }
#else
    for (size_t i = 0; i < total; ++i) {
        float s = g_manifold.buffer_final[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        output_s32[i] = (int32_t)(s * scale);
    }
#endif
}

void neuro_cochlear_manifold_teardown() {
    g_manifold.pipeline_active.store(false, std::memory_order_release);
    free(g_manifold.buffer_pre_hrtf);
    free(g_manifold.buffer_post_hrtf);
    free(g_manifold.buffer_post_up);
    free(g_manifold.buffer_final);
    delete g_manifold.dsp_client;
    delete g_manifold.upsampler;
    delete g_manifold.volterra;
    g_manifold.dsp_client = nullptr;
    g_manifold.upsampler = nullptr;
    g_manifold.volterra = nullptr;
    g_manifold.buffer_pre_hrtf = nullptr;
    g_manifold.buffer_post_hrtf = nullptr;
    g_manifold.buffer_post_up = nullptr;
    g_manifold.buffer_final = nullptr;
}

} // namespace dsp
} // namespace ivanna

namespace ivannuri {

NeuroCochlearManifold::NeuroCochlearManifold() noexcept
    : master_gain_(1.0),
      global_alpha_(0.15),
      global_beta_(0.10),
      global_gamma_(0.08),
      global_delta_(0.05),
      last_n_samples_(0),
      initialized_(false),
      lateral_strength_(0.18),
      ohc_comp_ratio_(0.35),
      ohc_attack_coeff_(0.05),
      ohc_release_coeff_(0.005),
      an_alpha_fast_(0.12),
      an_alpha_slow_(0.01),
      masking_decay_(0.985) {
    reset();
}

double NeuroCochlearManifold::_erbFromFc(double fc) noexcept {
    return 24.7 * (4.37 * (fc * 0.001) + 1.0);
}

inline double NeuroCochlearManifold::_fastTanh(double x) noexcept {
    if (!std::isfinite(x)) return 0.0;
    if (x > 4.5) return 1.0;
    if (x < -4.5) return -1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

void NeuroCochlearManifold::_computeGammatoneCoeffs(std::size_t ch) noexcept {
    if (ch >= N_CHANNELS) return;
    CochlearChannel& c = channels_[ch];
    const double frac = static_cast<double>(ch) / static_cast<double>(N_CHANNELS - 1);
    // Mapa Greenwood logarítmico 80 Hz .. 16000 Hz
    c.fc = 80.0 * std::pow(200.0, frac);
    c.erb_bw = _erbFromFc(c.fc);
    c.q_factor = std::clamp(c.fc / std::max(1.0, c.erb_bw), 1.2, 8.0);
    c.bm_stiffness = 1.0 + 0.5 * frac;

    const double w0 = TWO_PI_D * c.fc / SAMPLE_RATE;
    const double sin_w0 = std::sin(w0);
    const double cos_w0 = std::cos(w0);
    const double alpha = sin_w0 / (2.0 * c.q_factor);
    const double inv_a0 = 1.0 / (1.0 + alpha);

    for (std::size_t b = 0; b < 4; ++b) {
        c.biquads[b].b0 = (alpha)*inv_a0;
        c.biquads[b].b1 = 0.0;
        c.biquads[b].b2 = (-alpha) * inv_a0;
        c.biquads[b].a1 = (-2.0 * cos_w0) * inv_a0;
        c.biquads[b].a2 = (1.0 - alpha) * inv_a0;
        c.biquads[b].z1 = 0.0;
        c.biquads[b].z2 = 0.0;
    }
}

bool NeuroCochlearManifold::initialize(
    const double* h1_weights,
    const double* h2_weights,
    const double* h3_weights,
    const double* alpha_gains,
    const double* beta_gains,
    const double* gamma_gains,
    const double* delta_gains,
    const double* eta_decays) noexcept {
    for (std::size_t ch = 0; ch < N_CHANNELS; ++ch) {
        CochlearChannel& c = channels_[ch];
        _computeGammatoneCoeffs(ch);
        for (std::size_t t = 0; t < VOLTERRA_TAPS; ++t) {
            const std::size_t idx = ch * VOLTERRA_TAPS + t;
            c.volterra.h1[t] = h1_weights ? h1_weights[idx] : (t == 0 ? (1.0 / N_CHANNELS) : 0.0);
            c.volterra.h2[t] = h2_weights ? h2_weights[idx] : 0.002;
            c.volterra.h3[t] = h3_weights ? h3_weights[idx] : 0.0005;
            c.history[t] = 0.0;
        }
        c.hist_w = 0;
        c.np_state = 0.0;
        c.ie_state = 0.0;
        c.c_state  = 0.0;
        c.channel_gain = 1.0 / static_cast<double>(N_CHANNELS);
        c.alpha = alpha_gains ? alpha_gains[ch] : global_alpha_;
        c.beta  = beta_gains  ? beta_gains[ch]  : global_beta_;
        c.gamma = gamma_gains ? gamma_gains[ch] : global_gamma_;
        c.delta = delta_gains ? delta_gains[ch] : global_delta_;
        c.eta   = eta_decays  ? eta_decays[ch]  : 0.92;
        c.ihc.z1 = 0.0;
        c.ihc.lp_coeff = 0.85;
        c.ohc.env = 0.0;
        c.ohc.gain = 1.0;
        c.an.q = 1.0;
        c.an.w = 0.0;
        c.an.rate = 0.0;
        c.el_weight = 1.0;
        c.masking_env = 0.0;
        std::memset(c.output, 0, sizeof(c.output));
    }
    initialized_ = true;
    return true;
}

void NeuroCochlearManifold::reset() noexcept {
    std::memset(scratch_l_, 0, sizeof(scratch_l_));
    std::memset(scratch_r_, 0, sizeof(scratch_r_));
    std::memset(sum_buffer_, 0, sizeof(sum_buffer_));
    std::memset(gammatone_buf_, 0, sizeof(gammatone_buf_));
    std::memset(ihc_buf_, 0, sizeof(ihc_buf_));
    std::memset(inhibited_buf_, 0, sizeof(inhibited_buf_));
    std::memset(h_alpha_buf_, 0, sizeof(h_alpha_buf_));
    std::memset(h_gamma_buf_, 0, sizeof(h_gamma_buf_));
    std::memset(h_delta_buf_, 0, sizeof(h_delta_buf_));
    (void)initialize(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}

inline double NeuroCochlearManifold::_processGammatoneSample(std::size_t ch, double input) noexcept {
    CochlearChannel& c = channels_[ch];
    double x = input;
    for (std::size_t b = 0; b < 2; ++b) {
        GammatoneBiquad& bq = c.biquads[b];
        const double y = bq.b0 * x + bq.z1;
        bq.z1 = bq.b1 * x - bq.a1 * y + bq.z2;
        bq.z2 = bq.b2 * x - bq.a2 * y;
        x = y;
    }
    return x;
}

double NeuroCochlearManifold::_computeOHCGain(std::size_t ch, double bm) noexcept {
    CochlearChannel& c = channels_[ch];
    const double abs_bm = std::fabs(bm);
    const double coeff = (abs_bm > c.ohc.env) ? ohc_attack_coeff_ : ohc_release_coeff_;
    c.ohc.env += coeff * (abs_bm - c.ohc.env);
    c.ohc.gain = 1.0 / (1.0 + ohc_comp_ratio_ * c.ohc.env * 2.5);
    return std::clamp(c.ohc.gain, 0.25, 1.25);
}

inline double NeuroCochlearManifold::_computeIHC(std::size_t ch, double bm) noexcept {
    CochlearChannel& c = channels_[ch];
    const double rect = (bm > 0.0) ? bm : (0.15 * bm);
    c.ihc.z1 = c.ihc.lp_coeff * c.ihc.z1 + (1.0 - c.ihc.lp_coeff) * rect;
    return c.ihc.z1;
}

void NeuroCochlearManifold::_applyLateralInhibition(double* buf) noexcept {
    for (std::size_t ch = 0; ch < N_CHANNELS; ++ch) {
        double neigh = 0.0;
        if (ch > 0) neigh += 0.5 * buf[ch - 1];
        if (ch + 1 < N_CHANNELS) neigh += 0.5 * buf[ch + 1];
        inhibited_buf_[ch] = buf[ch] - lateral_strength_ * neigh;
    }
}

void NeuroCochlearManifold::_updateMeddisAN(std::size_t ch, double ihc_out) noexcept {
    CochlearChannel& c = channels_[ch];
    const double drive = std::max(0.0, ihc_out);
    c.an.rate = an_alpha_fast_ * drive + (1.0 - an_alpha_fast_) * c.an.rate;
    c.masking_env = masking_decay_ * c.masking_env + (1.0 - masking_decay_) * std::fabs(ihc_out);
}

inline void NeuroCochlearManifold::_pushHistory(std::size_t ch, double value) noexcept {
    CochlearChannel& c = channels_[ch];
    c.history[c.hist_w & (VOLTERRA_TAPS - 1)] = value;
    c.hist_w = (c.hist_w + 1) & (VOLTERRA_TAPS - 1);
}

double NeuroCochlearManifold::_processVolterra(std::size_t ch) noexcept {
    const CochlearChannel& c = channels_[ch];
    const std::size_t latest = (c.hist_w + VOLTERRA_TAPS - 1) & (VOLTERRA_TAPS - 1);
    const double x0 = c.history[latest];
    return c.volterra.h1[0] * x0 + c.volterra.h2[0] * (x0 * std::fabs(x0)) * 0.15;
}

void NeuroCochlearManifold::processBlock(
    const double* IVANNURI_RESTRICT inL,
    const double* IVANNURI_RESTRICT inR,
    double*       IVANNURI_RESTRICT outL,
    double*       IVANNURI_RESTRICT outR,
    std::size_t n_samples) noexcept {
    if (!inL || !inR || !outL || !outR || n_samples == 0) return;
    if (!initialized_) reset();

    const std::size_t n = std::min(n_samples, BLOCK_SIZE);
    last_n_samples_ = n;

    for (std::size_t i = 0; i < n; ++i) {
        const double xL = std::isfinite(inL[i]) ? inL[i] : 0.0;
        const double xR = std::isfinite(inR[i]) ? inR[i] : 0.0;
        const double mono = 0.5 * (xL + xR);

        double sumCorrection = 0.0;
        for (std::size_t ch = 0; ch < N_CHANNELS; ++ch) {
            const double bm = _processGammatoneSample(ch, mono);
            const double ohcG = _computeOHCGain(ch, bm);
            const double corrected = bm * ohcG;
            gammatone_buf_[ch] = corrected;
            ihc_buf_[ch] = _computeIHC(ch, corrected);
            _pushHistory(ch, corrected);
            _updateMeddisAN(ch, ihc_buf_[ch]);
            sumCorrection += _processVolterra(ch);
        }
        _applyLateralInhibition(ihc_buf_);

        const double corr = _fastTanh(sumCorrection * master_gain_) * 0.18;
        outL[i] = std::clamp(xL + corr, -0.998, 0.998);
        outR[i] = std::clamp(xR + corr, -0.998, 0.998);
    }
}

void NeuroCochlearManifold::setMasterGain(double gain_db) noexcept {
    master_gain_ = std::pow(10.0, std::clamp(gain_db, -24.0, 12.0) / 20.0);
}
void NeuroCochlearManifold::setChannelGain(std::size_t ch, double gain_db) noexcept {
    if (ch < N_CHANNELS) {
        channels_[ch].channel_gain = std::pow(10.0, std::clamp(gain_db, -24.0, 12.0) / 20.0) / N_CHANNELS;
    }
}
void NeuroCochlearManifold::setGlobalAlpha(double a) noexcept { global_alpha_ = std::clamp(a, 0.0, 2.0); }
void NeuroCochlearManifold::setGlobalBeta (double b) noexcept { global_beta_  = std::clamp(b, 0.0, 2.0); }
void NeuroCochlearManifold::setGlobalGamma(double g) noexcept { global_gamma_ = std::clamp(g, 0.0, 2.0); }
void NeuroCochlearManifold::setGlobalDelta(double d) noexcept { global_delta_ = std::clamp(d, 0.0, 2.0); }
void NeuroCochlearManifold::setLateralInhibition(double strength) noexcept {
    lateral_strength_ = std::clamp(strength, 0.0, 0.5);
}
void NeuroCochlearManifold::setOHCCompression(double ratio) noexcept {
    ohc_comp_ratio_ = std::clamp(ratio, 0.0, 1.0);
}
double NeuroCochlearManifold::getChannelEnergy(std::size_t channel) const noexcept {
    return (channel < N_CHANNELS) ? channels_[channel].ohc.env : 0.0;
}
double NeuroCochlearManifold::getChannelCenterFreq(std::size_t channel) const noexcept {
    return (channel < N_CHANNELS) ? channels_[channel].fc : 0.0;
}
double NeuroCochlearManifold::getANFiringRate(std::size_t channel) const noexcept {
    return (channel < N_CHANNELS) ? channels_[channel].an.rate : 0.0;
}
double NeuroCochlearManifold::getSpectralEntropy() const noexcept {
    double sum = 0.0;
    for (std::size_t ch = 0; ch < N_CHANNELS; ++ch) sum += channels_[ch].ohc.env;
    if (sum <= 1e-12) return 0.0;
    double h = 0.0;
    for (std::size_t ch = 0; ch < N_CHANNELS; ++ch) {
        const double p = channels_[ch].ohc.env / sum;
        if (p > 1e-12) h -= p * std::log2(p);
    }
    return h / std::log2(static_cast<double>(N_CHANNELS));
}
double NeuroCochlearManifold::getMaskingThreshold(std::size_t channel) const noexcept {
    return (channel < N_CHANNELS) ? channels_[channel].masking_env : 0.0;
}

} // namespace ivannuri

