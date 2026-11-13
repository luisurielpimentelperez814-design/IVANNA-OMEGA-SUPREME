// ============================================================================
// LEGACY — NO SE COMPILA. Núcleo polifásico Genspark v2.5.396 (18/5).
// Preservado por la regla "nunca borrar código". Fue reemplazado en la
// rectificación magistral (v2.5.397) por el núcleo RouteA + FIFO de bloque
// fijo (ivanna_asrc_core.{h,cpp}, 23/23). Este archivo se conserva tal cual
// estaba en el commit 5c1643de (con su cabecera, renombrada aquí para no
// chocar con la nueva). Diagnóstico del reemplazo en BUILD.md y en el
// documento de diseño privado (analysis-ivanna/DISENO_Daemon_ASRC_Magistral.md).
// ============================================================================
// ============================================================================
// ivanna_asrc_core.cpp — FIFO + polifásico con bloque fijo. v2.5.396
//
// El lazo de nivel del FIFO es la pieza que faltaba. Sin él, un ratio != 1
// devuelve un número de frames distinto al de entrada y el bloque se desgarra
// en cada frontera (medido en la v anterior: THD+N -29.98 dB). Con él:
//   - sale siempre in_frames
//   - la frecuencia sí se corrige (step = 1/ratio sobre las posiciones)
//   - la corrección de nivel se reparte en el bloque (sin clic)
// ============================================================================
#include "ivanna_asrc_core_genspark_legacy.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ivanna {
namespace asrc {

namespace {
constexpr double kPi = 3.14159265358979323846;

double sinc(double x) {
    if (std::fabs(x) < 1e-12) return 1.0;
    const double px = kPi * x;
    return std::sin(px) / px;
}

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double q = (x * x) / 4.0;
    for (int k = 1; k < 40; ++k) {
        term *= q / (static_cast<double>(k) * static_cast<double>(k));
        sum += term;
        if (term < 1e-16 * sum) break;
    }
    return sum;
}

double kaiser(double u, double half, double beta) {
    if (half <= 0.0) return 1.0;
    const double r = u / half;
    if (r < -1.0 || r > 1.0) return 0.0;
    const double a = besselI0(beta);
    if (a <= 0.0) return 1.0;
    return besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / a;
}
}  // namespace

bool ratioIsValid(double ratio) {
    if (!std::isfinite(ratio)) return false;
    return std::fabs((ratio - 1.0) * 1e6) <= kMaxPpm + 1e-9;
}

Core::Core(const Config& c) : cfg_(c) {
    if (cfg_.taps % 2 == 0) cfg_.taps += 1;
    if (cfg_.phases < 2) cfg_.phases = 2;
    if (cfg_.taps < 15) cfg_.taps = 15;
    if (cfg_.target_pending <= 0) cfg_.target_pending = cfg_.taps;
    if (cfg_.target_pending < cfg_.taps) cfg_.target_pending = cfg_.taps;
}

Core::~Core() = default;

void Core::reset() {
    // PRIMING. Un ASRC de bloque fijo necesita nacer con holgura: entre lo que
    // se evalúa (read_pos_) y el final del buffer tiene que caber el FIR por
    // delante (half) MÁS el margen de deriva de un bloque entero. Si nace justo
    // (319 frames, como antes), en el primer bloque con step>1 se agota y el
    // tramo final se rellena con CEROS: eso es un hueco de silencio por bloque
    // y sonaba a -27 dB de THD+N. Medido, no supuesto.
    const size_t taps = static_cast<size_t>(cfg_.taps);
    const size_t half = (taps - 1) / 2;
    //   latencia añadida   = read_pos_            (~taps + half = 478 muestras = 10 ms)
    //   holgura disponible = buf_frames_ - read_pos_ - half  (~taps)
    buf_.assign((2 * taps) * 2, 0.0f);
    buf_frames_ = 2 * taps;
    read_pos_ = static_cast<double>(taps + half);
}

bool Core::init() {
    if (inited_) return true;
    const int T = cfg_.taps;
    const int P = cfg_.phases;
    coef_.assign(static_cast<size_t>(T) * P, 0.0f);

    const double half = (T - 1) / 2.0;
    for (int p = 0; p < P; ++p) {
        const double d = static_cast<double>(p) / P;
        double sum = 0.0;
        for (int k = 0; k < T; ++k) {
            const double u = (k - half) - d;
            const double h = 2.0 * cfg_.cutoff * sinc(2.0 * cfg_.cutoff * u) *
                             kaiser(u, half, cfg_.kaiser_beta);
            coef_[static_cast<size_t>(p) * T + k] = static_cast<float>(h);
            sum += h;
        }
        // Normalización por fase: DC exacto en todas las sub-fases. Sin esto el
        // ratio fraccionario modularía el nivel (bombeo de amplitud).
        if (std::fabs(sum) > 1e-12) {
            const float inv = static_cast<float>(1.0 / sum);
            for (int k = 0; k < T; ++k)
                coef_[static_cast<size_t>(p) * T + k] *= inv;
        }
    }

    psi_.assign(static_cast<size_t>(cfg_.psi_taps) * 2, 0.0f);
    for (int ch = 0; ch < 2; ++ch)
        psi_[static_cast<size_t>(ch) * cfg_.psi_taps + cfg_.psi_taps / 2] = 1.0f;

    inited_ = true;
    channels_ = 2;
    reset();
    return true;
}

double Core::setRatio(double ratio) {
    if (!ratioIsValid(ratio)) { ratio_ = 1.0; return ratio_; }
    double dev = (ratio - 1.0) * 1e6;
    dev = std::max(-kMaxPpm, std::min(kMaxPpm, dev));
    ratio_ = 1.0 + dev * 1e-6;
    return ratio_;
}

void Core::ensureInit(int channels) {
    if (!inited_) init();
    if (channels != channels_) {
        channels_ = channels;
        reset();
        if (buf_.size() % static_cast<size_t>(channels) != 0 || channels != 2) {
            const size_t keep = buf_frames_;
            buf_.assign(keep * static_cast<size_t>(channels), 0.0f);
        }
    }
}

void Core::push(const float* inter, size_t frames, int ch) {
    const size_t need = (buf_frames_ + frames) * static_cast<size_t>(ch);
    if (buf_.size() < need) buf_.resize(need + static_cast<size_t>(cfg_.taps) * 2 * ch);
    std::memcpy(buf_.data() + buf_frames_ * ch, inter, frames * ch * sizeof(float));
    buf_frames_ += frames;
}

void Core::compact() {
    // Se conserva el contexto necesario antes de read_pos_ (half muestras).
    const double keep_from = std::floor(read_pos_) - (cfg_.taps - 1) / 2.0 - 2.0;
    if (keep_from <= 0.0) return;
    const size_t drop = static_cast<size_t>(keep_from);
    if (drop >= buf_frames_) return;
    const size_t ch = static_cast<size_t>(channels_);
    std::memmove(buf_.data(), buf_.data() + drop * ch,
                 (buf_frames_ - drop) * ch * sizeof(float));
    buf_frames_ -= drop;
    read_pos_ -= static_cast<double>(drop);
}

ProcessStats Core::emitFixed(const float* in, size_t in_frames,
                             float* out, size_t out_capacity, int ch) {
    ProcessStats st;
    st.in_frames = in_frames;
    ensureInit(ch);

    const int T = cfg_.taps;
    const int P = cfg_.phases;
    const int half = (T - 1) / 2;
    const size_t N = std::min(in_frames, out_capacity);

    // step base = 1/ratio. Con ratio>1 (reloj lento) se consumen MENOS muestras
    // por muestra de salida -> la señal se estira -> la frecuencia baja a su
    // valor nominal. Derivación completa en la cabecera.
    const double base_step = 1.0 / ratio_;

    // ── TEOREMA DEL BLOQUE FIJO ─────────────────────────────────────────────
    // Un efecto de AudioFlinger devuelve SIEMPRE los mismos frames que recibe.
    // Si sale N y entra N, y para corregir deriva se consumen N*step != N
    // muestras, la holgura del FIFO TIENE que derivar: es aritmética, no un bug.
    //
    // Dos errores MEDIDOS en las iteraciones previas de este archivo, y cómo se
    // corrigen aquí:
    //
    //  (a) Lazo proporcional "de nivel" calculado contra el objetivo:
    //      corr = 0.5*err/N con err = holgura - N*step - objetivo. Como
    //      err ~= N*(1-step), resulta corr ~= (1-step), es decir INVIERTE la
    //      corrección: el test midió 999.5 Hz, error -500 ppm (el riel entero).
    //      -> Prohibido. El término de nivel debe ser MUCHO menor que la
    //      desviación de step, no comparable.
    //
    //  (b) Desliz de 1 frame repartido en UN bloque: 1/4096 = 244 ppm, cinco
    //      veces el riel y 24x la corrección. El test lo midió: THD+N -27 dB
    //      a +50 ppm (y no a -20 ppm, porque sólo el paso positivo desliza
    //      hacia arriba). -> El desliz se reparte en MUCHOS bloques.
    //
    // Diseño final: step = 1/ratio EXACTO (aquí vive la corrección) + un
    // término de nivel acotado a ±2 ppm. La corrección nominal es de decenas de
    // ppm, así que el término de nivel no puede cancelarla ni de lejos; 2 ppm
    // de modulación de fase es -90 dB de artefacto, por debajo del ruido de
    // cuantización de 16 bits.
    const int Tf = cfg_.taps;
    const double half_f = (Tf - 1) / 2.0;
    const double slack_now = static_cast<double>(buf_frames_) - read_pos_ - half_f;
    const double slack_nominal = static_cast<double>(Tf);
    double step = base_step;

    // Cuánto cambia la holgura con este bloque (N == in_frames en operación).
    const double slack_after = slack_now + static_cast<double>(N) * (1.0 - base_step);
    const double slack_err = slack_after - slack_nominal;

    // Ganancia del lazo de nivel: reparte el error en ~400 bloques (~34 s) y se
    // satura en ±2 ppm. Con la ventana de 480 frames (10 ms) el término nunca
    // supera el tope, así que es un desplazamiento lento, no una corrección.
    double corr = slack_err / (static_cast<double>(N) * 400.0);
    const double kCorrMax = 2e-6;   // 2 ppm
    if (corr >  kCorrMax) corr =  kCorrMax;
    if (corr < -kCorrMax) corr = -kCorrMax;
    step += corr;

    push(in, in_frames, ch);

    const size_t avail = buf_frames_;
    size_t o = 0;
    while (o < N) {
        const long long base = static_cast<long long>(std::floor(read_pos_));
        const long long start = base - half;
        if (start < 0 || start + T > static_cast<long long>(avail)) break;

        const double frac = read_pos_ - static_cast<double>(base);
        int ph = static_cast<int>(frac * P + 0.5);
        if (ph >= P) ph = P - 1;
        if (ph < 0) ph = 0;

        const float* h = coef_.data() + static_cast<size_t>(ph) * T;
        for (int c = 0; c < ch; ++c) {
            const float* x = buf_.data() + static_cast<size_t>(start) * ch + c;
            float acc = 0.0f;
            for (int k = 0; k < T; ++k) {
                acc += h[k] * x[static_cast<size_t>(k) * ch];
            }
            out[o * ch + c] = acc;
        }
        read_pos_ += step;
        ++o;
    }

    // Relleno de emergencia: SÓLO si el priming falló o hubo un salto brusco
    // de ratio. Se cuenta en la telemetría (underflow_frames) porque en
    // operación normal debe ser exactamente 0. Se rellena REPITIENDO la última
    // muestra válida, no con ceros: un cero es un hueco de silencio audible y
    // además inyecta un escalón (eso era el -27 dB de THD+N).
    if (o < N) {
        st.underflow_frames = N - o;
        for (size_t i = o; i < N; ++i) {
            for (int c = 0; c < ch; ++c) {
                out[i * ch + c] = (o > 0) ? out[(o - 1) * ch + c] : 0.0f;
            }
        }
    }

    st.out_frames = N;
    st.step = step;
    st.fifo_frames = static_cast<double>(buf_frames_) - read_pos_;
    st.passthrough = false;
    compact();
    return st;
}

void Core::applyPsi(float* buf, size_t frames, int channels) {
    if (!psi_enabled_ || psi_.empty()) return;
    const int Np = cfg_.psi_taps;
    std::vector<float> tmp(frames * channels, 0.0f);
    for (int c = 0; c < channels; ++c) {
        const float* co = psi_.data() + static_cast<size_t>(c) * Np;
        for (size_t n = 0; n < frames; ++n) {
            float acc = 0.0f;
            for (int k = 0; k < Np; ++k) {
                const long long idx = static_cast<long long>(n) - k;
                if (idx < 0) break;
                acc += co[k] * buf[static_cast<size_t>(idx) * channels + c];
            }
            tmp[n * channels + c] = acc;
        }
    }
    std::memcpy(buf, tmp.data(), frames * channels * sizeof(float));
}

ProcessStats Core::processF32(const float* in, size_t in_frames,
                              float* out, size_t out_capacity, int channels) {
    ProcessStats st;
    st.in_frames = in_frames;
    if (!in || !out || in_frames == 0 || out_capacity == 0) return st;

    if (bypass_ || ratio_ == 1.0) {
        const size_t n = std::min(in_frames, out_capacity);
        std::memcpy(out, in, n * channels * sizeof(float));
        if (psi_enabled_) applyPsi(out, n, channels);
        st.out_frames = n;
        st.passthrough = !psi_enabled_;
        st.step = 1.0;
        st.fifo_frames = static_cast<double>(cfg_.taps);
        // Mantener el contexto real aunque se esté en identidad: si luego se
        // activa el ratio, el FIR debe tener historia de verdad, no ceros.
        ensureInit(channels);
        push(in, in_frames, channels);
        read_pos_ += static_cast<double>(in_frames);
        compact();
        return st;
    }
    return emitFixed(in, in_frames, out, out_capacity, channels);
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
        // La salida es bit-exacta (memcpy de arriba), pero el FIFO se mantiene
        // VIVO y en fase: se empuja la entrada y se avanza read_pos_ lo mismo
        // que entró. Así, si el ratio se activa en el bloque siguiente, el FIR
        // tiene señal REAL detrás en vez de 10 ms de ceros (que sonarían como
        // un hueco de silencio al arrancar la corrección).
        ensureInit(channels);
        std::vector<float> tmp(in_frames * channels);
        for (size_t i = 0; i < in_frames * channels; ++i)
            tmp[i] = static_cast<float>(in[i]) * (1.0f / 32768.0f);
        push(tmp.data(), in_frames, channels);
        read_pos_ += static_cast<double>(in_frames);
        compact();
        return st;
    }

    std::vector<float> fin(in_frames * channels);
    for (size_t i = 0; i < in_frames * channels; ++i)
        fin[i] = static_cast<float>(in[i]) * (1.0f / 32768.0f);

    std::vector<float> fout(out_capacity * channels, 0.0f);
    st = processF32(fin.data(), in_frames, fout.data(), out_capacity, channels);

    for (size_t i = 0; i < st.out_frames * channels; ++i) {
        float v = fout[i] * 32768.0f;
        v = std::max(-32768.0f, std::min(32767.0f, v));
        out[i] = static_cast<int16_t>(v);
    }
    return st;
}

}  // namespace asrc
}  // namespace ivanna
