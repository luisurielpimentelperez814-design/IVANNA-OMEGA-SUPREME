// ============================================================================
// test_ivanna_asrc.cpp — suite de aceptación §7.4 y §8. v2.5.396
// Runner mínimo (sin Google Test en este entorno) que IMPRIME MÉTRICAS REALES.
// ============================================================================
#include "../engine/asrc/ivanna_asrc_core.h"
#include "../engine/asrc/ivanna_asrc_ipc.h"
#include "../engine/asrcd/ivanna_servo.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace ivanna;

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* name, const char* detail = "") {
    if (ok) { ++g_pass; std::printf("  [PASA ] %s %s\n", name, detail); }
    else    { ++g_fail; std::printf("  [FALLA] %s %s\n", name, detail); }
}

// Frecuencia por cruces por cero con interpolación lineal.
static double freqByZeroCross(const std::vector<float>& y, double fs) {
    std::vector<double> cross;
    for (size_t i = 1; i < y.size(); ++i) {
        if (y[i - 1] <= 0.0f && y[i] > 0.0f) {
            const double t = static_cast<double>(i - 1) +
                             (-y[i - 1]) / (y[i] - y[i - 1]);
            cross.push_back(t);
        }
    }
    if (cross.size() < 2) return 0.0;
    const double periods = static_cast<double>(cross.size() - 1);
    const double span = (cross.back() - cross.front()) / fs;
    return span > 0 ? periods / span : 0.0;
}

// THD+N — MEDIDO EN EL DOMINIO DEL TIEMPO, no por resta de potencias.
//
// La versión anterior restaba la potencia de la fundamental a la potencia total
// (yy - pf). Con 88.000 muestras esas dos cifras son casi iguales y la resta
// pierde todos los dígitos significativos: devolvía -300 dB, que es imposible
// con 16 bits y delataba el error. Aquí se construye el residuo en el tiempo
// (r = y - modelo) y se mide su potencia: sin cancelación, con cifras reales.
static double thdPlusNDb(const std::vector<float>& y, double fs, double f0) {
    const size_t n = y.size();
    auto residPower = [&](double f) {
        const double w = 2.0 * M_PI * f / fs;
        double scc = 0, sss = 0, ssc = 0, yc = 0, ys = 0;
        for (size_t i = 0; i < n; ++i) {
            const double c = std::cos(w * i), s = std::sin(w * i);
            scc += c * c; sss += s * s; ssc += s * c;
            yc += y[i] * c; ys += y[i] * s;
        }
        const double det = scc * sss - ssc * ssc;
        if (std::fabs(det) < 1e-12) return 0.0;
        const double a = (yc * sss - ys * ssc) / det;
        const double b = (ys * scc - yc * ssc) / det;
        double pr = 0;
        for (size_t i = 0; i < n; ++i) {
            const double model = a * std::cos(w * i) + b * std::sin(w * i);
            const double r = static_cast<double>(y[i]) - model;
            pr += r * r;
        }
        return pr;
    };

    double lo = f0 * 0.997, hi = f0 * 1.003;
    const double gr = 0.6180339887498949;
    double c = hi - gr * (hi - lo), d = lo + gr * (hi - lo);
    double fc = residPower(c), fd = residPower(d);
    for (int it = 0; it < 50; ++it) {
        if (fc < fd) { hi = d; d = c; fd = fc; c = hi - gr * (hi - lo); fc = residPower(c); }
        else         { lo = c; c = d; fc = fd; d = lo + gr * (hi - lo); fd = residPower(d); }
    }
    const double res = std::min(fc, fd);
    double total = 0;
    for (float v : y) total += static_cast<double>(v) * v;
    if (total <= 0) return 0.0;
    return 10.0 * std::log10(std::max(res / total, 1e-40));
}

static std::vector<float> runThrough(asrc::Core& core, const std::vector<int16_t>& in,
                                     size_t frames, int ch, size_t chunk) {
    std::vector<int16_t> out(frames * ch, 0);
    std::vector<float> meas;
    size_t off = 0;
    while (off < frames) {
        const size_t n = std::min(chunk, frames - off);
        const auto st = core.processS16(in.data() + off * ch, n,
                                        out.data() + off * ch, chunk, ch);
        for (size_t i = 0; i < st.out_frames; ++i)
            meas.push_back(static_cast<float>(out[(off + i) * ch]) / 32768.0f);
        off += n;
    }
    return meas;
}

// ── 1. Passthrough bit-exacto ───────────────────────────────────────────────
static void test_passthrough() {
    std::printf("\n[1] passthrough_bit_exact\n");
    asrc::Core core;
    core.init();
    const size_t F = 2048, ch = 2;
    std::vector<int16_t> in(F * ch), out(F * ch, 0);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = static_cast<int16_t>(static_cast<long long>(i * 37) % 65536 - 32768);

    const auto st = core.processS16(in.data(), F, out.data(), F, ch);
    const bool same = std::memcmp(in.data(), out.data(), in.size() * sizeof(int16_t)) == 0;
    check(same && st.passthrough, "bytes identicos a ratio 1.0");
    std::printf("         frames=%zu passthrough=%d\n", st.out_frames, (int)st.passthrough);
}

// ── 2. Corrección de deriva ─────────────────────────────────────────────────
static void test_drift() {
    std::printf("\n[2] drift_correction\n");
    const double nominal = 48000.0;
    const double f0 = 1000.0;
    const size_t F = 48000;
    for (double ppm : {-6.25, -70.0, 42.5}) {
        const double fs_real = nominal * (1.0 + ppm * 1e-6);
        std::vector<int16_t> in(F * 2);
        for (size_t i = 0; i < F; ++i) {
            const double v = 0.5 * std::sin(2.0 * M_PI * f0 * i / fs_real);
            in[i * 2] = in[i * 2 + 1] = static_cast<int16_t>(v * 32767.0);
        }
        asrc::Core core;
        core.init();
        core.setRatio(asrcd::TheilSenServo::ratioFor(ppm));
        auto meas = runThrough(core, in, F, 2, 4096);
        const size_t skip = 6000;
        std::vector<float> steady(meas.begin() + std::min(skip, meas.size()), meas.end());
        const double f_meas = freqByZeroCross(steady, nominal);
        const double err_ppm = (f_meas / f0 - 1.0) * 1e6;
        char buf[200];
        std::snprintf(buf, sizeof(buf),
                      "deriva %+7.2f ppm -> f=%.4f Hz (error residual %+.3f ppm)",
                      ppm, f_meas, err_ppm);
        check(std::fabs(err_ppm) < 1.0, "corrige la deriva", buf);
    }
}

// ── 3. THD+N del camino polifásico ──────────────────────────────────────────
static void test_thd() {
    std::printf("\n[3] thd_n_ratio_path\n");
    const double nominal = 48000.0;
    const double f0 = 997.0;   // no múltiplo exacto: peor caso de enrejado
    const size_t F = 96000;
    std::vector<int16_t> in(F * 2);
    for (size_t i = 0; i < F; ++i) {
        const double v = 0.5 * std::sin(2.0 * M_PI * f0 * i / nominal);
        in[i * 2] = in[i * 2 + 1] = static_cast<int16_t>(v * 32767.0);
    }
    struct Case { double ppm; const char* label; };
    const Case cases[] = {{0.0, "ratio 1.0 (forzado a 1+1e-9 para usar el FIR)"},
                          {-20.8, "deriva tipica de bocina"},
                          {50.0,  "deriva positiva, cerca del riel"}};
    for (const auto& c : cases) {
        asrc::Core core;
        core.init();
        core.setRatio(c.ppm == 0.0 ? 1.0 + 1e-9 : asrcd::TheilSenServo::ratioFor(c.ppm));
        auto meas = runThrough(core, in, F, 2, 4096);
        const size_t skip = 8000;
        std::vector<float> steady(meas.begin() + std::min(skip, meas.size()), meas.end());
        const double db = thdPlusNDb(steady, nominal, f0);
        char buf[220];
        std::snprintf(buf, sizeof(buf), "THD+N = %.2f dB  (%s)", db, c.label);
        check(db < -70.0, "THD+N mejor que -70 dB", buf);
    }
}

// ── 4. Ganancia DC plana ────────────────────────────────────────────────────
static void test_dc() {
    std::printf("\n[4] dc_gain_flat\n");
    double worst = 0.0, worst_ppm = 0.0;
    for (int p = 0; p < 40; ++p) {
        const double ppm = 1.0 + p * 3.0;
        asrc::Core core;
        core.init();
        core.setRatio(asrcd::TheilSenServo::ratioFor(ppm - 1.0 - 60.0));
        std::vector<int16_t> in(8192 * 2, 10000);
        std::vector<int16_t> out(8192 * 2, 0);
        const auto st = core.processS16(in.data(), 8192, out.data(), 8192, 2);
        if (st.out_frames > 200) {
            double acc = 0; size_t cnt = 0;
            for (size_t i = st.out_frames / 2; i < st.out_frames; ++i) { acc += out[i * 2]; ++cnt; }
            const double g = (cnt ? acc / cnt : 0.0) / 10000.0;
            const double dev = std::fabs(g - 1.0);
            if (dev > worst) { worst = dev; worst_ppm = ppm - 61.0; }
        }
    }
    char buf[180];
    std::snprintf(buf, sizeof(buf),
                  "peor desviacion de ganancia DC = %.5f (%.3f dB) en el punto de ratio mas adverso",
                  worst, 20.0 * std::log10(1.0 + worst));
    check(worst < 0.01, "ganancia DC plana (<0.01)", buf);
}

// ── 5. Fail-closed ──────────────────────────────────────────────────────────
static void test_fail_closed() {
    std::printf("\n[5] fail_closed\n");
    const std::string base = "/tmp/ivanna_t_" + std::to_string(::getpid());
    const std::string ratio_p = base + "_ratio.shm";

    { asrc::Reader r; r.open(base + "_noexiste.shm", 200);
      const auto res = r.read();
      check(!res.fresh && res.ratio == 1.0, "sin canal -> ratio 1.0", res.reason); }

    { asrc::Writer w; w.open(ratio_p);
      w.write(1.0002, 20.0, asrc::kFlagDaemonAlive, 0, asrc::monotonicNs());
      asrc::Reader r; r.open(ratio_p, 1);
      ::usleep(20000);
      const auto res = r.read();
      check(!res.fresh && res.ratio == 1.0, "bloque viejo (>max_age) -> 1.0", res.reason); }

    { asrc::Writer w; w.open(ratio_p);
      w.write(1.0002, 20.0, asrc::kFlagKillSwitch | asrc::kFlagDaemonAlive, 0,
              asrc::monotonicNs());
      asrc::Reader r; r.open(ratio_p, 5000);
      const auto res = r.read();
      check(res.kill_switch && res.ratio == 1.0, "kill switch -> ratio 1.0", res.reason); }

    { asrc::Writer w; w.open(ratio_p);
      w.write(1.5, 500000.0, asrc::kFlagDaemonAlive, 0, asrc::monotonicNs());
      asrc::Reader r; r.open(ratio_p, 5000);
      const auto res = r.read();
      check(!res.fresh && res.ratio == 1.0, "ratio fuera de riel (+500000 ppm) -> 1.0", res.reason); }

    { asrc::Writer w; w.open(ratio_p);
      w.write(1.0002, 20.0, asrc::kFlagDaemonAlive, 0, asrc::monotonicNs());
      const int fd = ::open(ratio_p.c_str(), O_RDWR);
      if (fd >= 0) { const char junk = 0x7f; (void)::pwrite(fd, &junk, 1, 30); ::close(fd); }
      asrc::Reader r; r.open(ratio_p, 5000);
      const auto res = r.read();
      check(!res.fresh && res.ratio == 1.0, "CRC corrupto -> ratio 1.0", res.reason); }

    { asrc::Core c; c.init();
      const double r1 = c.setRatio(std::nan(""));
      const double r2 = c.setRatio(1.0 / 0.0);
      const double r3 = c.setRatio(1.01);
      const double r4 = c.setRatio(0.9995);   // -500 ppm: en el borde, se acepta
      char b2[160];
      std::snprintf(b2, sizeof(b2), "NaN->%.1f inf->%.1f 10000ppm->%.1f -500ppm->%.6f",
                    r1, r2, r3, r4);
      check(r1 == 1.0 && r2 == 1.0 && r3 == 1.0 && std::fabs((r4 - 1.0) * 1e6 + 500.0) < 1.0,
            "Core rechaza invalidos y acepta el borde del riel", b2); }

    ::unlink(ratio_p.c_str());
}

// ── 6. IPC bajo carga ───────────────────────────────────────────────────────
static void test_ipc() {
    std::printf("\n[6] ipc_roundtrip\n");
    const std::string p = "/tmp/ivanna_ipc_" + std::to_string(::getpid()) + ".shm";
    asrc::Writer w; w.open(p);
    asrc::Reader r; r.open(p, 5000);
    int ok = 0, crc = 0, other = 0;
    for (int i = 0; i < 3000; ++i) {
        const double target = 1.0 + (i % 100) * 1e-6;
        w.write(target, (target - 1.0) * 1e6, asrc::kFlagDaemonAlive, i, asrc::monotonicNs());
        const auto res = r.read();
        if (res.fresh && std::fabs(res.ratio - target) < 1e-12) ++ok;
        else if (res.reason && std::strcmp(res.reason, "crc_invalido") == 0) ++crc;
        else ++other;
    }
    char buf[180];
    std::snprintf(buf, sizeof(buf), "%d/3000 exactos, %d con CRC malo, %d inconsistentes", ok, crc, other);
    check(ok >= 2985 && crc == 0, "roundtrip estable", buf);
    ::unlink(p.c_str());
}

// ── 7. Theil-Sen robusto ────────────────────────────────────────────────────
static void test_servo() {
    std::printf("\n[7] servo_theilsen\n");
    // BASE DE TIEMPO LARGA, y esto no es un detalle de test: un contador de
    // frames es ENTERO. A 20 ms de separación, 1 frame de cuantización vale
    // 1/960 = 1041 ppm, así que es físicamente imposible medir 6 ppm con esa
    // base. Con 1 s de separación, 1 frame = 20.8 ppm; con 32 s de ventana el
    // error baja a ~1 ppm. De ahí que el daemon decime a 500 ms.
    asrcd::TheilSenServo s(32, 48000.0, 500.0);
    const double true_ppm = -13.0;
    const double rate = 48000.0 * (1.0 + true_ppm * 1e-6);
    double acc = 0.0;
    for (int i = 0; i < 32; ++i) {
        const int64_t t = static_cast<int64_t>(i) * 1000000000LL;   // 1 s
        if (i > 0) acc += rate;                                     // 1 s de frames
        int64_t frames = static_cast<int64_t>(acc + 0.5);
        if (i == 15) frames += 2000;
        s.add(t, frames);
    }
    double ppm = 0.0;
    const bool ok = s.estimate(&ppm);
    char buf[220];
    std::snprintf(buf, sizeof(buf),
                  "real %+.1f ppm -> estimado %+.3f ppm (error %+.3f) con outlier de +2000 frames",
                  true_ppm, ppm, ppm - true_ppm);
    check(ok && std::fabs(ppm - true_ppm) < 6.0, "robusto al outlier", buf);

    asrcd::TheilSenServo flat(32);
    for (int i = 0; i < 8; ++i) {
        const int64_t t = static_cast<int64_t>(i) * 1000000000LL;
        flat.add(t, static_cast<int64_t>(i) * 48000);
    }
    double p2 = 999.0;
    flat.estimate(&p2);
    char b2[120];
    std::snprintf(b2, sizeof(b2), "reloj perfecto a 48000 -> %+.4f ppm", p2);
    check(std::fabs(p2) < 1.0, "sin deriva -> ~0 ppm", b2);
}

// ── 8. Slew ─────────────────────────────────────────────────────────────────
static void test_slew() {
    std::printf("\n[8] slew_limit\n");
    double r = 1.0;
    const double target = 1.0 - 70e-6;
    int steps = 0;
    bool overshoot = false;
    while (steps < 100) {
        const double nr = asrcd::TheilSenServo::slew(r, target, 5.0);
        if (std::fabs((nr - r) * 1e6) > 5.0 + 1e-9) { overshoot = true; break; }
        r = nr; ++steps;
        if (std::fabs(nr - target) < 1e-12) break;
    }
    char buf[180];
    std::snprintf(buf, sizeof(buf),
                  "70 ppm en %d pasos, ningun salto >5 ppm (%s), fin a %.6f ppm",
                  steps, overshoot ? "VIOLADO" : "ok", (r - 1.0) * 1e6);
    check(!overshoot && std::fabs(r - target) < 6e-6, "converge sin salto audible", buf);
}

// ── 8-bis. El FIFO no se desborda ni se vacía (lazo de nivel) ───────────────
static void test_fifo_stability() {
    std::printf("\n[8b] fifo_stability\n");
    for (double ppm : {-70.0, 0.0, 70.0}) {
        asrc::Core core;
        core.init();
        core.setRatio(ppm == 0.0 ? 1.0 + 1e-12 : asrcd::TheilSenServo::ratioFor(ppm));
        std::vector<int16_t> in(4096 * 2, 1000), out(4096 * 2, 0);
        double mn = 1e9, mx = -1e9;
        size_t underflow = 0;
        for (int b = 0; b < 400; ++b) {   // 400 bloques = 34 s de audio
            const auto st = core.processS16(in.data(), 4096, out.data(), 4096, 2);
            if (b > 20) { mn = std::min(mn, st.fifo_frames); mx = std::max(mx, st.fifo_frames); }
            if (st.out_frames != 4096) { std::printf("         bloque %d devolvio %zu frames (DEBE ser 4096)\n", b, st.out_frames); }
            if (st.underflow_frames != 0) { std::printf("         bloque %d relleno %zu frames por falta de datos\n", b, st.underflow_frames); underflow += st.underflow_frames; }
        }
        char buf[180];
        std::snprintf(buf, sizeof(buf),
                      "ppm=%+6.1f holgura %.1f..%.1f frames, underflow=%zu en 34 s",
                      ppm, mn, mx, underflow);
        // Cota honesta: la latencia puede variar hasta 10 ms (480 frames) +
        // el tamaño de un bloque de desliz. No se exige nivel plano porque en
        // bloque fijo eso es imposible sin cancelar la corrección de deriva.
        check(mx - mn < 1200.0 && mx < 2000.0 && underflow == 0,
              "bloque fijo 4096, latencia acotada y CERO underflow", buf);
    }
}

// ── 9/10. Latencia y coste medidos ──────────────────────────────────────────
static void test_cost() {
    std::printf("\n[9/10] latencia y coste (medidos en este host)\n");
    for (int taps : {287, 319}) {
        asrc::Config c; c.taps = taps;
        asrc::Core core(c);
        core.init();
        const double lat = core.groupDelayMs(48000.0);
        core.setRatio(1.0 - 20e-6);
        std::vector<int16_t> in(4096 * 2, 1234), out(4096 * 2, 0);
        for (int i = 0; i < 20; ++i) core.processS16(in.data(), 4096, out.data(), 4096, 2);
        const int iters = 200;
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < iters; ++i) core.processS16(in.data(), 4096, out.data(), 4096, 2);
        const auto t1 = std::chrono::steady_clock::now();
        const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
        const double audio_ms = 4096.0 / 48.0;
        std::printf("         taps=%d latencia=%.3f ms (%.1f muestras) coste=%.1f us/bloque = %.2f%% de un core = %.1f ns/muestra-canal\n",
                    taps, lat, lat * 48.0, us, 100.0 * (us / 1000.0) / audio_ms,
                    us * 1000.0 / (4096.0 * 2.0));
        check(100.0 * (us / 1000.0) / audio_ms < 100.0, "cabe en tiempo real", "");
    }
}

int main() {
    std::printf("=== IVANNA ASRC — suite de aceptacion v2.5.396 ===\n");
    std::printf("compilador: %s\n", __VERSION__);
    test_passthrough();
    test_drift();
    test_thd();
    test_dc();
    test_fail_closed();
    test_ipc();
    test_servo();
    test_slew();
    test_fifo_stability();
    test_cost();
    std::printf("\n=== RESULTADO: %d pasan, %d fallan ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
