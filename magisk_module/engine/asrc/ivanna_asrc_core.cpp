// ============================================================================
// ivanna_asrc_core.cpp — Núcleo RouteA + FIFO de bloque fijo. v2.5.397
// (rectificación magistral; el core Genspark v2.5.396 queda como LEGACY en
// ivanna_asrc_core_genspark_legacy.cpp y NO se compila.)
//
// La frecuencia la fija el step = 1/ratio del núcleo RouteA (exacto, sin
// lazo de nivel que la sesgue). El bloque fijo lo garantiza la FIFO de
// salida: el resampleador produce N*ratio frames por bloque y la FIFO emite
// exactamente N. El nivel de la FIFO deriva N*(ratio-1) por bloque (física:
// es la diferencia de relojes acumulada) y se acota con priming (192
// frames de ceros al arrancar) + slip de 1 frame en los watermarks.
// ============================================================================
#include "ivanna_asrc_core.h"
#include "routea_core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ivanna {
namespace asrc {

namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

bool ratioIsValid(double ratio) {
    if (!std::isfinite(ratio)) return false;
    return std::fabs((ratio - 1.0) * 1e6) <= kMaxPpm + 1e-9;
}

struct Core::PairResampler {
    routea::Core core;
    std::vector<float> in;   // estéreo intercalado, in_frames frames
    std::vector<float> out;  // estéreo intercalado, capacidad de salida
};

Core::Core(const Config& c) : cfg_(c) {
    if (cfg_.taps % 2 == 0) cfg_.taps += 1;
    if (cfg_.phases < 2) cfg_.phases = 2;
    if (cfg_.taps < 15) cfg_.taps = 15;
    if (cfg_.target_pending <= 0) cfg_.target_pending = cfg_.taps;
    if (cfg_.target_pending < cfg_.taps) cfg_.target_pending = cfg_.taps;
}

Core::~Core() = default;

void Core::reset() {
    for (auto& p : pairs_) p.core.reset();
    primeFifo();
    was_bypass_ = true;
}

void Core::primeFifo() {
    // La FIFO se llena de ceros hasta el priming. Esos ceros son la
    // latencia de arranque (4 ms, una sola vez): la salida real empieza
    // tras ellos. En bypass (ratio 1.0) la FIFO no se toca (memcpy).
    if (fifo_.size() < kFifoCapacityFrames * static_cast<size_t>(channels_))
        fifo_.assign(kFifoCapacityFrames * static_cast<size_t>(channels_), 0.0f);
    std::fill(fifo_.begin(), fifo_.end(), 0.0f);
    fifo_head_ = 0;
    fifo_frames_ = kFifoPrimeFrames;
}

bool Core::init() {
    if (inited_) return true;
    channels_ = 2;
    fifo_.assign(kFifoCapacityFrames * static_cast<size_t>(channels_), 0.0f);
    primeFifo();
    inited_ = true;
    return true;
}

double Core::setRatio(double ratio) {
    if (!ratioIsValid(ratio)) { ratio_ = 1.0; return ratio_; }
    double dev = (ratio - 1.0) * 1e6;
    dev = std::max(-kMaxPpm, std::min(kMaxPpm, dev));
    ratio_ = 1.0 + dev * 1e-6;
    for (auto& p : pairs_) p.core.setRatio(ratio_);
    return ratio_;
}

void Core::ensureInit(int channels) {
    if (!inited_) init();
    if (channels != channels_) {
        channels_ = channels;
        pairs_.clear();
        fifo_.assign(kFifoCapacityFrames * static_cast<size_t>(channels_), 0.0f);
        primeFifo();
        was_bypass_ = true;
    }
}

void Core::fifoPush(const float* inter, size_t frames) {
    if (frames == 0 || !inter) return;
    const size_t cap = kFifoCapacityFrames;
    const size_t ch = static_cast<size_t>(channels_);
    // Nunca debe desbordar (nivel máx ~5637 < 8192); si lo hiciera, se
    // descarta lo más viejo (fail-closed: no se rompe el anillo).
    if (fifo_frames_ + frames > cap) {
        const size_t drop = (fifo_frames_ + frames) - cap;
        fifoDrop(drop);
        slips_total_ += drop;
    }
    size_t tail = (fifo_head_ + fifo_frames_) % cap;
    const size_t first = std::min(frames, cap - tail);
    std::memcpy(fifo_.data() + tail * ch, inter, first * ch * sizeof(float));
    if (first < frames) {
        std::memcpy(fifo_.data(), inter + first * ch,
                    (frames - first) * ch * sizeof(float));
    }
    fifo_frames_ += frames;
}

void Core::fifoDrop(size_t frames) {
    if (frames == 0) return;
    if (frames > fifo_frames_) frames = fifo_frames_;
    fifo_head_ = (fifo_head_ + frames) % kFifoCapacityFrames;
    fifo_frames_ -= frames;
}

size_t Core::fifoPop(float* out, size_t frames) {
    if (frames == 0 || !out) return 0;
    const size_t take = std::min(frames, fifo_frames_);
    if (take == 0) return 0;
    const size_t cap = kFifoCapacityFrames;
    const size_t ch = static_cast<size_t>(channels_);
    const size_t first = std::min(take, cap - fifo_head_);
    std::memcpy(out, fifo_.data() + fifo_head_ * ch, first * ch * sizeof(float));
    if (first < take) {
        std::memcpy(out + first * ch, fifo_.data(),
                    (take - first) * ch * sizeof(float));
    }
    fifo_head_ = (fifo_head_ + take) % cap;
    fifo_frames_ -= take;
    return take;
}

size_t Core::resampleIntoFifo(const float* in, size_t in_frames, int ch) {
    const size_t npairs = (static_cast<size_t>(ch) + 1) / 2;
    if (pairs_.size() != npairs) {
        pairs_.clear();
        pairs_.resize(npairs);
        for (auto& p : pairs_) p.core.setRatio(ratio_);
    }
    // Capacidad de salida del resampleador: N*ratio_max + margen.
    const int capOut = static_cast<int>(in_frames * 1002 / 1000 + 64);
    if (scratch_in_.size() < in_frames * 2)
        scratch_in_.resize(in_frames * 2);
    if (scratch_out_.size() < static_cast<size_t>(capOut) * 2)
        scratch_out_.resize(static_cast<size_t>(capOut) * 2);
    if (staging_.size() < in_frames * static_cast<size_t>(ch))
        staging_.resize(in_frames * static_cast<size_t>(ch));

    size_t produced_min = static_cast<size_t>(capOut) + 1;
    // Por par: desintercalar, resamplear, guardar en el staging intercalado.
    for (size_t p = 0; p < npairs; ++p) {
        const int c0 = static_cast<int>(p * 2);
        const int c1 = (c0 + 1 < ch) ? c0 + 1 : c0;  // canal impar: duplicado
        auto& pr = pairs_[p];
        if (pr.in.size() < in_frames * 2) pr.in.resize(in_frames * 2);
        if (pr.out.size() < static_cast<size_t>(capOut) * 2)
            pr.out.resize(static_cast<size_t>(capOut) * 2);
        for (size_t i = 0; i < in_frames; ++i) {
            pr.in[i * 2] = in[i * static_cast<size_t>(ch) + c0];
            pr.in[i * 2 + 1] = in[i * static_cast<size_t>(ch) + c1];
        }
        const int produced = pr.core.process(pr.in.data(), static_cast<int>(in_frames),
                                             pr.out.data(), capOut);
        if (produced < 0) continue;
        if (static_cast<size_t>(produced) < produced_min)
            produced_min = static_cast<size_t>(produced);
        // Intercalar este par en el staging (solo hasta produced_min se usa;
        // se escribe el par completo y se recorta al meter en la FIFO).
        for (int i = 0; i < produced; ++i) {
            staging_[static_cast<size_t>(i) * static_cast<size_t>(ch) + c0] =
                pr.out[static_cast<size_t>(i) * 2];
            if (c1 != c0)
                staging_[static_cast<size_t>(i) * static_cast<size_t>(ch) + c1] =
                    pr.out[static_cast<size_t>(i) * 2 + 1];
        }
    }
    if (produced_min > static_cast<size_t>(capOut)) produced_min = 0;
    if (produced_min == 0) return 0;
    fifoPush(staging_.data(), produced_min);
    return produced_min;
}

ProcessStats Core::processF32(const float* in, size_t in_frames,
                              float* out, size_t out_capacity, int channels) {
    ProcessStats st;
    st.in_frames = in_frames;
    if (!in || !out || in_frames == 0 || out_capacity == 0) return st;

    ensureInit(channels);
    const size_t ch = static_cast<size_t>(channels_);
    const size_t N = std::min(in_frames, out_capacity);

    // Camino rápido (identidad): memcpy bit-exacto. La FIFO no se toca y
    // el resampleador NO se alimenta en sombra (el bypass debe ser barato;
    // al volver al resampleo se arranca en frío, ver abajo).
    if (bypass_ || ratio_ == 1.0) {
        std::memcpy(out, in, N * ch * sizeof(float));
        st.out_frames = N;
        st.passthrough = true;
        st.step = 1.0;
        st.fifo_frames = static_cast<double>(fifo_frames_);
        st.underflow_frames = 0;
        st.slip_frames = 0;
        was_bypass_ = true;
        return st;
    }

    // Transición bypass -> resampleo: arranque en frío (fade-in del FIR,
    // ~6 ms, sin click) y re-priming de la FIFO. El historial anterior es
    // obsoleto (era de antes del bypass) y no se reutiliza.
    if (was_bypass_) {
        for (auto& p : pairs_) p.core.reset();
        primeFifo();
        was_bypass_ = false;
    }

    resampleIntoFifo(in, in_frames, channels_);

    // Slip por watermark alto: el nivel post-emisión no debe superar el
    // watermark. El exceso crece <=0.3 frames/bloque, así que como mucho
    // cae 1 frame por llamada. El frame descartado es el más viejo (la
    // salida salta 1 muestra: un slip raro, inaudible, contado).
    size_t slips = 0;
    while (fifo_frames_ > N + kFifoHighWatermark) {
        fifoDrop(1);
        ++slips;
        ++slips_total_;
    }

    // Emitir exactamente N frames desde la FIFO.
    const size_t take = fifoPop(out, N);
    size_t underflow = 0;
    if (take < N) {
        underflow = N - take;
        for (size_t i = take; i < N; ++i) {
            for (size_t c = 0; c < ch; ++c)
                out[i * ch + c] = (take > 0) ? out[(take - 1) * ch + c] : 0.0f;
        }
    }

    st.out_frames = N;
    st.passthrough = false;
    st.step = 1.0 / ratio_;
    st.fifo_frames = static_cast<double>(fifo_frames_);
    st.underflow_frames = underflow;
    st.slip_frames = slips;
    return st;
}

ProcessStats Core::processS16(const int16_t* in, size_t in_frames,
                              int16_t* out, size_t out_capacity, int channels) {
    ProcessStats st;
    st.in_frames = in_frames;
    if (!in || !out || in_frames == 0 || out_capacity == 0) return st;

    // Camino rápido para el caso dominante (identidad): ni se convierten las
    // muestras. Es lo que garantiza 0 LSB de diferencia con lo que entrega
    // AudioFlinger (test 1).
    if ((bypass_ || ratio_ == 1.0) && !psi_enabled_) {
        const size_t n = std::min(in_frames, out_capacity);
        std::memcpy(out, in, n * channels * sizeof(int16_t));
        st.out_frames = n;
        st.passthrough = true;
        st.step = 1.0;
        st.fifo_frames = static_cast<double>(fifo_frames_);
        st.underflow_frames = 0;
        st.slip_frames = 0;
        was_bypass_ = true;
        return st;
    }

    const size_t ch = static_cast<size_t>(channels);
    std::vector<float> fin(in_frames * ch);
    for (size_t i = 0; i < in_frames * ch; ++i)
        fin[i] = static_cast<float>(in[i]) * (1.0f / 32768.0f);

    std::vector<float> fout(out_capacity * ch, 0.0f);
    st = processF32(fin.data(), in_frames, fout.data(), out_capacity, channels);

    for (size_t i = 0; i < st.out_frames * ch; ++i) {
        float v = fout[i] * 32768.0f;
        v = std::max(-32768.0f, std::min(32767.0f, v));
        out[i] = static_cast<int16_t>(v);
    }
    return st;
}

}  // namespace asrc
}  // namespace ivanna
