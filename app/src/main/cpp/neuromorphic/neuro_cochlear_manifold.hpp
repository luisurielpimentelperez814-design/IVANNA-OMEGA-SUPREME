// © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E — All rights reserved.
// Proprietary and confidential. Embedded copyright; do not strip.
// Verify with ivannanpe::verifyCopyrightIntegrity() at boot.
#pragma once
#include "../include/ivanna_npe_license.h"

#include <cstddef>
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef IVANNURI_ASSERT
    #include <cassert>
    #define IVANNURI_ASSERT(x) assert(x)
#endif

// ── Portabilidad: MSVC / GCC / Clang ───────────────────────────────────────
#if defined(_MSC_VER)
    #define IVANNURI_ALWAYS_INLINE __forceinline
    #define IVANNURI_RESTRICT      __restrict
    #define IVANNURI_UNLIKELY(x)   (x)
#elif defined(__GNUC__) || defined(__clang__)
    #define IVANNURI_ALWAYS_INLINE __attribute__((always_inline))
    #define IVANNURI_RESTRICT      __restrict__
    #define IVANNURI_UNLIKELY(x)   __builtin_expect(!!(x), 0)
#else
    #define IVANNURI_ALWAYS_INLINE inline
    #define IVANNURI_RESTRICT
    #define IVANNURI_UNLIKELY(x)   (x)
#endif

namespace ivannuri {

// ── Constantes de configuración (ajústalas y recompila) ──────────────────────
constexpr std::size_t N_CHANNELS      = 32;   // Número de bandas cocleares
constexpr std::size_t VOLTERRA_TAPS  = 16;   // MUST ser potencia de 2
constexpr std::size_t BLOCK_SIZE     = 512;  // Máximo tamaño de bloque por llamada
constexpr double      SAMPLE_RATE    = 48000.0;
constexpr std::size_t RK4_SUBSTEPS   = 4;
constexpr double      DT_RK4         = 1.0 / (SAMPLE_RATE * static_cast<double>(RK4_SUBSTEPS));
constexpr double      PI_D           = 3.14159265358979323846;
constexpr double      TWO_PI_D       = 6.28318530717958647692;

// ── Estructuras internas ─────────────────────────────────────────────────────
struct GammatoneBiquad {
    double b0, b1, b2;
    double a1, a2;
    double z1, z2;
};

struct VolterraKernel {
    double h1[VOLTERRA_TAPS];
    double h2[VOLTERRA_TAPS];
    double h3[VOLTERRA_TAPS];
};

struct IHCState {
    double z1;
    double lp_coeff;
};

struct OHCState {
    double env;
    double gain;
};

struct ANState {
    double q;
    double w;
    double rate;
};

struct CochlearChannel {
    GammatoneBiquad biquads[4];
    VolterraKernel  volterra;
    double          history[VOLTERRA_TAPS];
    std::size_t     hist_w;
    double          np_state;
    double          ie_state;
    double          c_state;
    double          fc;
    double          erb_bw;
    double          q_factor;
    double          bm_stiffness;
    double          channel_gain;
    double          alpha, beta, gamma, delta, eta;
    IHCState        ihc;
    OHCState        ohc;
    ANState         an;
    double          el_weight;
    double          masking_env;
    double          output[BLOCK_SIZE];
};

// ── Clase principal ──────────────────────────────────────────────────────────
class NeuroCochlearManifold {
public:
    NeuroCochlearManifold() noexcept;

    [[nodiscard]]
    bool initialize(
        const double* h1_weights,
        const double* h2_weights,
        const double* h3_weights,
        const double* alpha_gains,
        const double* beta_gains,
        const double* gamma_gains,
        const double* delta_gains,
        const double* eta_decays) noexcept;

    void processBlock(
        const double* IVANNURI_RESTRICT inL,
        const double* IVANNURI_RESTRICT inR,
        double*       IVANNURI_RESTRICT outL,
        double*       IVANNURI_RESTRICT outR,
        std::size_t n_samples) noexcept;

    void reset() noexcept;

    // Setters de parámetros (seguros en tiempo real)
    void setMasterGain(double gain_db) noexcept;
    void setChannelGain(std::size_t ch, double gain_db) noexcept;
    void setGlobalAlpha(double a) noexcept;
    void setGlobalBeta (double b) noexcept;
    void setGlobalGamma(double g) noexcept;
    void setGlobalDelta(double d) noexcept;
    void setLateralInhibition(double strength) noexcept;
    void setOHCCompression(double ratio) noexcept;

    // Diagnósticos (NO llamar dentro de processBlock)
    double getChannelEnergy(std::size_t channel) const noexcept;
    double getChannelCenterFreq(std::size_t channel) const noexcept;
    double getANFiringRate(std::size_t channel) const noexcept;
    double getSpectralEntropy() const noexcept;
    double getMaskingThreshold(std::size_t channel) const noexcept;

private:
    // Procesado interno
    IVANNURI_ALWAYS_INLINE double _computeIHC(std::size_t ch, double bm) noexcept;
    double _computeOHCGain(std::size_t ch, double bm) noexcept;
    void   _applyLateralInhibition(double* buf) noexcept;
    void   _updateMeddisAN(std::size_t ch, double ihc_out) noexcept;
    void   _computeGammatoneCoeffs(std::size_t ch) noexcept;
    IVANNURI_ALWAYS_INLINE double _processGammatoneSample(std::size_t ch, double input) noexcept;
    double _processVolterra(std::size_t ch) noexcept;
    IVANNURI_ALWAYS_INLINE void _pushHistory(std::size_t ch, double value) noexcept;
    double _dynamics(std::size_t ch, double NP, double N_t, double IE,
                     double h_alpha, double h_gamma, double h_delta) noexcept;
    void   _computeLSTMGains(std::size_t ch, double& h_alpha, double& h_gamma, double& h_delta) noexcept;
    double _erbFromFc(double fc) noexcept;
    IVANNURI_ALWAYS_INLINE double _fastTanh(double x) noexcept;

    // Estado
    CochlearChannel channels_[N_CHANNELS];
    double scratch_l_[BLOCK_SIZE];
    double scratch_r_[BLOCK_SIZE];
    double sum_buffer_[BLOCK_SIZE];
    double gammatone_buf_[N_CHANNELS];
    double ihc_buf_[N_CHANNELS];
    double inhibited_buf_[N_CHANNELS];
    double h_alpha_buf_[N_CHANNELS];
    double h_gamma_buf_[N_CHANNELS];
    double h_delta_buf_[N_CHANNELS];

    // Parámetros
    double master_gain_;
    double global_alpha_;
    double global_beta_;
    double global_gamma_;
    double global_delta_;
    std::size_t last_n_samples_;
    bool initialized_;
    double lateral_strength_;
    double ohc_comp_ratio_;
    double ohc_attack_coeff_;
    double ohc_release_coeff_;
    double an_alpha_fast_;
    double an_alpha_slow_;
    double masking_decay_;
};

} // namespace ivannuri
