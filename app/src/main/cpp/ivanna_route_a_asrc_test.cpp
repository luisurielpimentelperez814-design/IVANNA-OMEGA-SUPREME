// ivanna_route_a_asrc_test.cpp — Tests de host del ASRC magistral Ruta A.
//
// Compila con g++ de host (sin NEON: usa el dot escalar) y corre sin
// Android. Valida:
//  (a) Tasa correcta para un ratio dado (+100 ppm → salida ~0.01% más rápida).
//  (b) THD+N del remuestreo con tono 1 kHz (meta < −90 dB), a ratio 1.0
//      y a +100 ppm (con la frecuencia de salida desplazada que corresponde).
//  (c) Continuidad entre bloques: procesar en bloques de 2048 frames da
//      la misma salida que de una sola vez (sin discontinuidad de fase).
//  (d) Fail-closed: ppm NaN → ratio 1.0; ppm fuera de rango → clamp
//      ±1000 ppm; Ψ apagada A MANO (setPsiActive(false)) da bypass exacto.
//  (e) FIR Ψ: convolución correcta (identidad y retardo de 32 muestras).
//  (f) Ψ VIVA por defecto (2026-11-11): la app diseña los taps con PsiCore
//      y abre la puerta en el primer bloque (sin daemon): la salida con Ψ
//      difiere de la de bypass, y psiFirTaps() devuelve taps.
//  (g) Taps Ψ: finitos y acotados (|tap| ≤ 4.0, el clamp de designFir).
//  (h) η se mueve con la coherencia (Ec. 4): sube con señal AM a 4 Hz
//      (coherencia alta) y baja con tono constante (coherencia ~0).
//  (i) Retardo de grupo con Ψ activa: el pico del impulso se desplaza
//      ~32 muestras respecto al bypass.
//  (j) THD+N con Ψ activa < −90 dB a +100 ppm (el campo no debe degradar
//      la limpieza del remuestreo).
//  (k) Captura (v2.5.412): THD+N con bloques de 320 frames (tamaño de
//      bloque de captura) a ratio 1.0 y +100 ppm, Ψ apagada y Ψ activa
//      congelada (misma meta < −90 dB que (b)/(j)).
//  (l) Captura: inFrames variable 320/319 (micro-slips de MasterTiming):
//      salida finita, sin clicks, y el número de frames sigue a la entrada.
//  (m) Captura: Ψ viva con bloques de 320 (activa tras el primer bloque,
//      η se mueve con AM 4 Hz, ILD real a azimut 30°).
//  (n) Captura: coste por bloque de 320 frames (ASRC+Ψ juntos):
//      media/p99/máx en µs; la cadena en vivo tiene 5.67 ms de
//      presupuesto (gobernador 85%) y el ASRC debe caber en ~60 µs
//      en el teléfono (el 410 midió ~40 µs en host).
//
// Uso: g++ -std=c++17 -O2 -I<hal_probe_dir> ivanna_route_a_asrc.cpp
//      ivanna_route_a_asrc_test.cpp -o test && ./test
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "ivanna_route_a_asrc.h"

using ivanna::routea::RouteAAsrc;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 48000;
constexpr double kToneHz = 1000.0;
constexpr double kAmp = 0.8;

int failures = 0;

void check(bool ok, const char* name) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

// Genera un seno estéreo intercalado (L = seno, R = coseno para probar
// ambos canales con fases distintas).
void genSine(std::vector<float>& buf, int frames, double freqHz, double phase0 = 0.0) {
    buf.resize(static_cast<size_t>(frames) * 2);
    for (int n = 0; n < frames; ++n) {
        double ph = 2.0 * kPi * freqHz * n / kRate + phase0;
        buf[n * 2] = static_cast<float>(kAmp * std::sin(ph));
        buf[n * 2 + 1] = static_cast<float>(kAmp * std::cos(ph));
    }
}

// Tono con modulación de amplitud a `modHz` (profundidad 0.8): la
// envolvente oscila en la banda 1–8 Hz → coherencia alta (enganche η).
// Sin AM, la envolvente es constante → coherencia ~0 (desenganche).
void genAmTone(std::vector<float>& buf, int frames, double freqHz, double modHz) {
    buf.resize(static_cast<size_t>(frames) * 2);
    for (int n = 0; n < frames; ++n) {
        double t = static_cast<double>(n) / kRate;
        double am = 1.0 + 0.8 * std::sin(2.0 * kPi * modHz * t);
        double ph = 2.0 * kPi * freqHz * t;
        buf[n * 2] = static_cast<float>(kAmp * 0.7 * am * std::sin(ph));
        buf[n * 2 + 1] = static_cast<float>(kAmp * 0.7 * am * std::cos(ph));
    }
}

// Impulso unitario en el frame 0 (ambos canales), resto silencio.
void genImpulse(std::vector<float>& buf, int frames) {
    buf.assign(static_cast<size_t>(frames) * 2, 0.0f);
    buf[0] = 1.0f;
    buf[1] = 1.0f;
}

// Índice del pico (|x| máximo) del canal L.
int peakIndexL(const std::vector<float>& buf) {
    int best = 0;
    float bestV = -1.0f;
    int n = static_cast<int>(buf.size() / 2);
    for (int i = 0; i < n; ++i) {
        float v = std::fabs(buf[i * 2]);
        if (v > bestV) { bestV = v; best = i; }
    }
    return best;
}

// Ajuste de seno por mínimos cuadrados (A*sin + B*cos) y THD+N:
// residual = señal − ajuste; THD+N = 20*log10(rms(residual)/rms(ajuste)).
// Se salta `skip` muestras al inicio y al final (settle del FIR + bordes).
double thdnDb(const float* x, int n, double freqHz, int skip) {
    double ss = 0.0, sc = 0.0, sss = 0.0, scc = 0.0, ssc = 0.0;
    double sx = 0.0, sxS = 0.0, sxC = 0.0;
    int m = 0;
    for (int i = skip; i < n - skip; ++i) {
        double ph = 2.0 * kPi * freqHz * i / kRate;
        double s = std::sin(ph), c = std::cos(ph);
        double v = x[i];
        ss += s * s; sc += c * c; sss += s * s * s; (void)sss;
        scc += s * c; ssc += s * c; (void)ssc;
        sx += v; sxS += v * s; sxC += v * c;
        ++m;
    }
    // Normal equations 2x2 para [A, B] (sin término DC: el seno no tiene DC
    // y el FIR no genera DC; el residuo incluye cualquier DC como ruido).
    double det = ss * sc - scc * scc;
    if (std::fabs(det) < 1e-12) return 0.0;
    double A = (sxS * sc - sxC * scc) / det;
    double B = (sxC * ss - sxS * scc) / det;
    double res2 = 0.0, sig2 = 0.0;
    for (int i = skip; i < n - skip; ++i) {
        double ph = 2.0 * kPi * freqHz * i / kRate;
        double fit = A * std::sin(ph) + B * std::cos(ph);
        double r = x[i] - fit;
        res2 += r * r;
        sig2 += fit * fit;
    }
    if (sig2 <= 0.0) return 0.0;
    return 10.0 * std::log10(res2 / sig2);
}

// Procesa `in` en bloques de `blockFrames` y devuelve toda la salida.
std::vector<float> processBlocked(RouteAAsrc& asrc, const std::vector<float>& in,
                                  int inFrames, int blockFrames) {
    std::vector<float> out;
    out.reserve(static_cast<size_t>(inFrames) * 2 + 4096);
    std::vector<float> scratch(static_cast<size_t>(blockFrames) * 2 + 4096);
    int done = 0;
    while (done < inFrames) {
        int n = inFrames - done;
        if (n > blockFrames) n = blockFrames;
        int cap = n * 1002 / 1000 + 64;
        int produced = asrc.process(in.data() + done * 2, n, scratch.data(), cap);
        out.insert(out.end(), scratch.begin(), scratch.begin() + produced * 2);
        done += n;
    }
    return out;
}

void testRate() {
    std::printf("== (a) Tasa correcta ==\n");
    // +100 ppm: 10 s de entrada → salida ~0.01% más larga.
    const int inFrames = 10 * kRate;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(100.0);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        double ratio = static_cast<double>(outFrames) / inFrames;
        std::printf("  +100 ppm: in=%d out=%d ratio=%.6f (esperado 1.000100)\n",
                    inFrames, outFrames, ratio);
        check(std::fabs(ratio - 1.0001) < 0.00002, "+100 ppm → ratio 1.0001 ±20 ppm");
    }
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        double ratio = static_cast<double>(outFrames) / inFrames;
        std::printf("  0 ppm: in=%d out=%d ratio=%.6f (esperado 1.000000)\n",
                    inFrames, outFrames, ratio);
        check(std::fabs(ratio - 1.0) < 0.00002, "0 ppm → ratio 1.0 ±20 ppm");
    }
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(-500.0);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        double ratio = static_cast<double>(outFrames) / inFrames;
        std::printf("  -500 ppm: in=%d out=%d ratio=%.6f (esperado 0.999500)\n",
                    inFrames, outFrames, ratio);
        check(std::fabs(ratio - 0.9995) < 0.00002, "-500 ppm → ratio 0.9995 ±20 ppm");
    }
}

void testThdn() {
    std::printf("== (b) THD+N (tono 1 kHz, meta < -90 dB) ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    // Canal L (seno) y R (coseno): medir ambos.
    // Ψ apagada A MANO aquí: (b) mide el remuestreador SOLO; la ganancia
    // de Γ (que sigue a η) derivaría y se leería como THD+N. La medida
    // con Ψ activa y diseño congelado está en (j).
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        asrc.setPsiActive(false);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        std::vector<float> chL(outFrames), chR(outFrames);
        for (int i = 0; i < outFrames; ++i) {
            chL[i] = out[i * 2];
            chR[i] = out[i * 2 + 1];
        }
        double tL = thdnDb(chL.data(), outFrames, kToneHz, 2000);
        double tR = thdnDb(chR.data(), outFrames, kToneHz, 2000);
        std::printf("  ratio 1.0: THD+N L=%.1f dB  R=%.1f dB\n", tL, tR);
        check(tL < -90.0 && tR < -90.0, "THD+N < -90 dB a ratio 1.0");
    }
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(100.0);
        asrc.setPsiActive(false);  // (b) mide el remuestreador SOLO
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        // A +100 ppm la salida se estira: 1 kHz → 1000/1.0001 = 999.9 Hz.
        double fOut = kToneHz / 1.0001;
        std::vector<float> chL(outFrames), chR(outFrames);
        for (int i = 0; i < outFrames; ++i) {
            chL[i] = out[i * 2];
            chR[i] = out[i * 2 + 1];
        }
        double tL = thdnDb(chL.data(), outFrames, fOut, 2000);
        double tR = thdnDb(chR.data(), outFrames, fOut, 2000);
        std::printf("  +100 ppm: THD+N L=%.1f dB  R=%.1f dB (f=%.2f Hz)\n", tL, tR, fOut);
        check(tL < -90.0 && tR < -90.0, "THD+N < -90 dB a +100 ppm");
    }
}

void testBlockContinuity() {
    std::printf("== (c) Continuidad entre bloques ==\n");
    const int inFrames = 8 * 2048;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    std::vector<float> outOne, outBlocked;
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(100.0);
        std::vector<float> scratch(static_cast<size_t>(inFrames) * 2 + 4096);
        int cap = inFrames * 1002 / 1000 + 64;
        int produced = asrc.process(in.data(), inFrames, scratch.data(), cap);
        outOne.assign(scratch.begin(), scratch.begin() + produced * 2);
    }
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(100.0);
        outBlocked = processBlocked(asrc, in, inFrames, 2048);
    }
    int n1 = static_cast<int>(outOne.size() / 2);
    int n2 = static_cast<int>(outBlocked.size() / 2);
    std::printf("  una vez: %d frames; por bloques: %d frames\n", n1, n2);
    check(n1 == n2, "mismo número de frames (una vez vs por bloques)");
    double maxDiff = 0.0;
    int n = n1 < n2 ? n1 : n2;
    for (int i = 0; i < n * 2; ++i) {
        double d = std::fabs(static_cast<double>(outOne[i]) - outBlocked[i]);
        if (d > maxDiff) maxDiff = d;
    }
    std::printf("  max |una vez − por bloques| = %.3e\n", maxDiff);
    check(maxDiff < 1e-5, "sin discontinuidad de fase entre bloques (<1e-5)");
    // Click en los bordes de bloque: la mayor diferencia entre muestras
    // consecutivas cerca de cada borde debe ser la del seno (no un salto).
    double maxStep = 0.0;
    double sineStep = kAmp * 2.0 * kPi * kToneHz / kRate;  // pendiente máx del seno
    for (int b = 1; b < 8; ++b) {
        int edge = b * 2048;  // borde aproximado (la salida no es exactamente 2048/bloque)
        for (int i = edge - 2; i <= edge + 2 && i + 1 < n; ++i) {
            if (i < 0) continue;
            double s = std::fabs(static_cast<double>(outBlocked[(i + 1) * 2]) -
                                 outBlocked[i * 2]);
            if (s > maxStep) maxStep = s;
        }
    }
    std::printf("  max paso en bordes de bloque = %.4f (pendiente seno = %.4f)\n",
                maxStep, sineStep);
    check(maxStep < sineStep * 1.5, "sin click en los bordes de bloque");
}

void testFailClosed() {
    std::printf("== (d) Fail-closed ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(std::nan(""));
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        double ratio = static_cast<double>(outFrames) / inFrames;
        std::printf("  ppm=NaN: ratio=%.6f (esperado 1.0)\n", ratio);
        check(std::fabs(ratio - 1.0) < 0.00002, "ppm NaN → ratio 1.0");
    }
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(5000.0);  // fuera de rango → clamp +1000
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        double ratio = static_cast<double>(outFrames) / inFrames;
        std::printf("  ppm=+5000: ratio=%.6f (esperado 1.001000, clamp)\n", ratio);
        check(std::fabs(ratio - 1.001) < 0.00002, "ppm +5000 → clamp +1000 ppm");
    }
    {
        // Ψ apagada A MANO (setPsiActive(false)) en ambas instancias:
        // la salida es idéntica (bypass exacto). OJO: por defecto Ψ es
        // ACTIVA (la app la diseña sola); el bypass solo ocurre con
        // apagado explícito o con setPsiAuto(false).
        RouteAAsrc a1, a2;
        a1.setClockCorrectionPpm(0.0);
        a2.setClockCorrectionPpm(0.0);
        a1.setPsiActive(false);
        a2.setPsiActive(false);
        std::vector<float> o1 = processBlocked(a1, in, inFrames, 2048);
        std::vector<float> o2 = processBlocked(a2, in, inFrames, 2048);
        double maxDiff = 0.0;
        int n = static_cast<int>(o1.size() < o2.size() ? o1.size() : o2.size());
        for (int i = 0; i < n; ++i) {
            double d = std::fabs(static_cast<double>(o1[i]) - o2[i]);
            if (d > maxDiff) maxDiff = d;
        }
        std::printf("  Ψ apagada a mano: max diff = %.3e\n", maxDiff);
        check(maxDiff == 0.0, "Ψ apagada a mano = bypass exacto");
    }
}

void testPsiFir() {
    std::printf("== (e) FIR Ψ ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    // Identidad: tap[0]=1, resto 0 → salida = entrada (tras el remuestreo).
    // La referencia es bypass REAL (setPsiActive(false)): por defecto Ψ es
    // ACTIVA y la referencia auto-diseñaría el campo.
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        float firL[64] = {}, firR[64] = {};
        firL[0] = 1.0f;
        firR[0] = 1.0f;
        asrc.setPsiFir(firL, firR);
        asrc.setPsiActive(true);
        std::vector<float> outPsi = processBlocked(asrc, in, inFrames, 2048);
        RouteAAsrc ref;
        ref.setClockCorrectionPpm(0.0);
        ref.setPsiActive(false);  // bypass real
        std::vector<float> outRef = processBlocked(ref, in, inFrames, 2048);
        double maxDiff = 0.0;
        int n = static_cast<int>(outPsi.size() < outRef.size() ? outPsi.size() : outRef.size());
        for (int i = 0; i < n; ++i) {
            double d = std::fabs(static_cast<double>(outPsi[i]) - outRef[i]);
            if (d > maxDiff) maxDiff = d;
        }
        std::printf("  Ψ identidad: max diff vs sin-Ψ = %.3e\n", maxDiff);
        check(maxDiff < 1e-6, "Ψ identidad = passthrough");
    }
    // Retardo: tap[32]=1 → salida = entrada retardada 32 muestras.
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        float firL[64] = {}, firR[64] = {};
        firL[32] = 1.0f;
        firR[32] = 1.0f;
        asrc.setPsiFir(firL, firR);
        asrc.setPsiActive(true);
        std::vector<float> outPsi = processBlocked(asrc, in, inFrames, 2048);
        RouteAAsrc ref;
        ref.setClockCorrectionPpm(0.0);
        ref.setPsiActive(false);  // bypass real
        std::vector<float> outRef = processBlocked(ref, in, inFrames, 2048);
        // Comparar outPsi[n] con outRef[n-32] (a partir de n=32+settle).
        double maxDiff = 0.0;
        int n = static_cast<int>(outPsi.size() / 2);
        for (int i = 2000 + 32; i < n - 100; ++i) {
            double dL = std::fabs(static_cast<double>(outPsi[i * 2]) - outRef[(i - 32) * 2]);
            double dR = std::fabs(static_cast<double>(outPsi[i * 2 + 1]) - outRef[(i - 32) * 2 + 1]);
            if (dL > maxDiff) maxDiff = dL;
            if (dR > maxDiff) maxDiff = dR;
        }
        std::printf("  Ψ retardo 32: max diff = %.3e\n", maxDiff);
        check(maxDiff < 1e-5, "Ψ retardo 32 muestras exacto");
    }
}

void testPsiViva() {
    std::printf("== (f) Ψ VIVA por defecto (la app diseña los taps) ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genAmTone(in, inFrames, kToneHz, 4.0);
    // Auto (default) vs apagada a mano: la salida DEBE diferir (el campo
    // está convolucionando: retardo 32 + respuesta del campo).
    RouteAAsrc autoAsrc, offAsrc;
    autoAsrc.setClockCorrectionPpm(0.0);
    offAsrc.setClockCorrectionPpm(0.0);
    offAsrc.setPsiActive(false);
    std::vector<float> outAuto = processBlocked(autoAsrc, in, inFrames, 2048);
    std::vector<float> outOff = processBlocked(offAsrc, in, inFrames, 2048);
    std::printf("  Ψ activa tras procesar: %s\n", autoAsrc.psiActive() ? "SI" : "NO");
    check(autoAsrc.psiActive(), "Ψ activa por defecto tras el primer bloque");
    check(!offAsrc.psiActive(), "Ψ apagada a mano sigue apagada");
    double maxDiff = 0.0;
    int n = static_cast<int>(outAuto.size() < outOff.size() ? outAuto.size() : outOff.size());
    for (int i = 0; i < n; ++i) {
        double d = std::fabs(static_cast<double>(outAuto[i]) - outOff[i]);
        if (d > maxDiff) maxDiff = d;
    }
    std::printf("  max |Ψ activa − bypass| = %.4f (tono 0.8)\n", maxDiff);
    check(maxDiff > 0.01, "Ψ activa cambia la salida (diff medible, no idéntica)");
    // Los taps diseñados existen y se pueden leer.
    float firL[64] = {}, firR[64] = {};
    bool got = autoAsrc.psiFirTaps(firL, firR);
    std::printf("  psiFirTaps() = %s\n", got ? "SI" : "NO");
    check(got, "psiFirTaps() devuelve los taps diseñados");
    // Con azimut 30° el campo es asimétrico: un tono mono (L==R) debe
    // salir con L != R (ILD real del campo, no solo retardo).
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        asrc.setPsiGeometry(-1.0f, -1.0f, 0.5236f);  // 30° a la derecha
        std::vector<float> mono;
        genSine(mono, inFrames, kToneHz);
        for (int i = 0; i < inFrames; ++i) mono[i * 2 + 1] = mono[i * 2];  // L==R
        std::vector<float> out = processBlocked(asrc, mono, inFrames, 2048);
        int outFrames = static_cast<int>(out.size() / 2);
        double rmsL = 0.0, rmsR = 0.0;
        for (int i = 2000; i < outFrames - 100; ++i) {
            rmsL += static_cast<double>(out[i * 2]) * out[i * 2];
            rmsR += static_cast<double>(out[i * 2 + 1]) * out[i * 2 + 1];
        }
        double ildDb = 10.0 * std::log10((rmsL + 1e-12) / (rmsR + 1e-12));
        std::printf("  azimut 30°: ILD de salida L/R = %.2f dB (mono de entrada)\n", ildDb);
        check(std::fabs(ildDb) > 0.5, "azimut 30° produce ILD real (L != R)");
    }
}

void testPsiTaps() {
    std::printf("== (g) Taps Ψ finitos y acotados ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genAmTone(in, inFrames, kToneHz, 4.0);
    RouteAAsrc asrc;
    asrc.setClockCorrectionPpm(0.0);
    // Procesar para que se diseñen los taps (primer bloque ya diseña).
    std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
    (void)out;
    float firL[64] = {}, firR[64] = {};
    check(asrc.psiFirTaps(firL, firR), "taps disponibles tras procesar");
    double maxAbs = 0.0;
    bool allFinite = true;
    for (int i = 0; i < 64; ++i) {
        if (!std::isfinite(firL[i]) || !std::isfinite(firR[i])) allFinite = false;
        double a = std::fabs(static_cast<double>(firL[i]));
        double b = std::fabs(static_cast<double>(firR[i]));
        if (a > maxAbs) maxAbs = a;
        if (b > maxAbs) maxAbs = b;
    }
    std::printf("  max |tap| = %.4f (límite 4.0); finitos: %s\n", maxAbs,
                allFinite ? "SI" : "NO");
    check(allFinite, "todos los taps son finitos");
    check(maxAbs <= 4.0, "|tap| ≤ 4.0 (clamp de designFir)");
    // El tap central (32) debe dominar (FIR de fase lineal, retardo 32).
    std::printf("  tap[32] L=%.4f R=%.4f (pico del FIR)\n", firL[32], firR[32]);
    check(std::fabs(firL[32]) > 0.1 && std::fabs(firR[32]) > 0.1,
          "tap central dominante (fase lineal, retardo 32)");
}

void testPsiEta() {
    std::printf("== (h) η se mueve con la coherencia (Ec. 4) ==\n");
    // Alta coherencia: AM a 4 Hz durante 3 s → η debe subir de 0.5.
    {
        const int inFrames = 3 * kRate;
        std::vector<float> in;
        genAmTone(in, inFrames, kToneHz, 4.0);
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        std::printf("  η inicial = %.3f, coherencia inicial = %.3f\n",
                    asrc.psiEta(), asrc.psiCoherence());
        check(std::fabs(asrc.psiEta() - 0.5) < 1e-9, "η inicial = 0.5");
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        (void)out;
        std::printf("  AM 4 Hz, 3 s: η = %.3f, coherencia = %.3f\n",
                    asrc.psiEta(), asrc.psiCoherence());
        check(asrc.psiEta() > 0.6, "η sube con coherencia alta (> 0.6)");
        check(asrc.psiCoherence() > 0.5, "coherencia medida alta (> 0.5)");
    }
    // Baja coherencia: tono constante (envolvente plana) durante 3 s,
    // instancia nueva → η debe bajar de 0.5.
    {
        const int inFrames = 3 * kRate;
        std::vector<float> in;
        genSine(in, inFrames, kToneHz);
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        (void)out;
        std::printf("  tono constante, 3 s: η = %.3f, coherencia = %.3f\n",
                    asrc.psiEta(), asrc.psiCoherence());
        check(asrc.psiEta() < 0.3, "η baja con coherencia ~0 (< 0.3)");
        check(asrc.psiCoherence() < 0.2, "coherencia medida baja (< 0.2)");
    }
    // Silencio: sin medida fiable, η se queda (fail-closed, no se inventa).
    {
        const int inFrames = 2 * kRate;
        std::vector<float> in(static_cast<size_t>(inFrames) * 2, 0.0f);
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
        (void)out;
        std::printf("  silencio, 2 s: η = %.3f (debe quedarse en 0.5)\n", asrc.psiEta());
        check(std::fabs(asrc.psiEta() - 0.5) < 1e-9, "η no se mueve en silencio");
    }
}

void testPsiGroupDelay() {
    std::printf("== (i) Retardo de grupo con Ψ activa (~32 muestras) ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genImpulse(in, inFrames);
    RouteAAsrc autoAsrc, offAsrc;
    autoAsrc.setClockCorrectionPpm(0.0);
    offAsrc.setClockCorrectionPpm(0.0);
    offAsrc.setPsiActive(false);
    std::vector<float> outAuto = processBlocked(autoAsrc, in, inFrames, 2048);
    std::vector<float> outOff = processBlocked(offAsrc, in, inFrames, 2048);
    int peakAuto = peakIndexL(outAuto);
    int peakOff = peakIndexL(outOff);
    std::printf("  pico bypass = %d, pico Ψ activa = %d, diferencia = %d (esperado ~32)\n",
                peakOff, peakAuto, peakAuto - peakOff);
    check(std::abs((peakAuto - peakOff) - 32) <= 3,
          "retardo de grupo Ψ ≈ 32 muestras (±3)");
}

void testPsiThdn() {
    std::printf("== (j) THD+N con Ψ activa (+100 ppm, meta < -90 dB) ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    RouteAAsrc asrc;  // Ψ viva por defecto (auto)
    asrc.setClockCorrectionPpm(100.0);
    // Calentar un bloque para que Ψ se diseñe y active, y CONGELAR el
    // diseño: la ganancia de Γ sigue a η y derivaría durante la medida
    // (eso se leería como THD+N aunque no es distorsión armónica; la
    // deriva lenta de Γ es el campo vivo, no el remuestreo). Con el diseño
    // congelado se mide la cadena estática (remuestreo + FIR Ψ fijo).
    {
        std::vector<float> warm(2048 * 2);
        asrc.process(in.data(), 2048, warm.data(), 2048 * 1002 / 1000 + 64);
    }
    std::printf("  Ψ activa antes de medir: %s (η=%.3f)\n",
                asrc.psiActive() ? "SI" : "NO", asrc.psiEta());
    check(asrc.psiActive(), "Ψ estaba activa durante la medida");
    asrc.setPsiDesignHold(true);
    std::vector<float> out = processBlocked(asrc, in, inFrames, 2048);
    int outFrames = static_cast<int>(out.size() / 2);
    double fOut = kToneHz / 1.0001;
    std::vector<float> chL(outFrames), chR(outFrames);
    for (int i = 0; i < outFrames; ++i) {
        chL[i] = out[i * 2];
        chR[i] = out[i * 2 + 1];
    }
    double tL = thdnDb(chL.data(), outFrames, fOut, 2000);
    double tR = thdnDb(chR.data(), outFrames, fOut, 2000);
    std::printf("  Ψ activa +100 ppm: THD+N L=%.1f dB  R=%.1f dB (f=%.2f Hz)\n",
                tL, tR, fOut);
    check(tL < -90.0 && tR < -90.0, "THD+N < -90 dB con Ψ activa a +100 ppm");
}

void testCaptureThdn() {
    std::printf("== (k) Captura: THD+N con bloques de 320 frames ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    auto measure = [&](RouteAAsrc& asrc, double fOut) {
        std::vector<float> out = processBlocked(asrc, in, inFrames, 320);
        int outFrames = static_cast<int>(out.size() / 2);
        std::vector<float> chL(outFrames), chR(outFrames);
        for (int i = 0; i < outFrames; ++i) {
            chL[i] = out[i * 2];
            chR[i] = out[i * 2 + 1];
        }
        double tL = thdnDb(chL.data(), outFrames, fOut, 2000);
        double tR = thdnDb(chR.data(), outFrames, fOut, 2000);
        std::printf("  THD+N L=%.1f dB  R=%.1f dB (f=%.2f Hz)\n", tL, tR, fOut);
        return tL < -90.0 && tR < -90.0;
    };
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        asrc.setPsiActive(false);  // remuestreador solo
        check(measure(asrc, kToneHz), "captura 320f: THD+N < -90 dB a ratio 1.0");
    }
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(100.0);
        asrc.setPsiActive(false);
        check(measure(asrc, kToneHz / 1.0001),
              "captura 320f: THD+N < -90 dB a +100 ppm");
    }
    {
        // Ψ activa con diseño congelado (como (j), pero a 320 frames).
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(100.0);
        std::vector<float> warm(320 * 2);
        asrc.process(in.data(), 320, warm.data(), 320 * 1002 / 1000 + 64);
        check(asrc.psiActive(), "captura 320f: Ψ activa tras el primer bloque");
        asrc.setPsiDesignHold(true);
        check(measure(asrc, kToneHz / 1.0001),
              "captura 320f: THD+N < -90 dB con Ψ activa a +100 ppm");
    }
}

void testCaptureVariableBlocks() {
    std::printf("== (l) Captura: bloques variables 320/319 (micro-slips) ==\n");
    const int totalFrames = 10 * kRate;
    std::vector<float> in;
    genSine(in, totalFrames, kToneHz);
    RouteAAsrc asrc;
    asrc.setClockCorrectionPpm(0.0);
    asrc.setPsiActive(false);  // el slip es del ASRC, no de Ψ
    std::vector<float> out;
    out.reserve(static_cast<size_t>(totalFrames) * 2 + 4096);
    std::vector<float> scratch(320 * 2 + 4096);
    int done = 0, blockIdx = 0, zeroBlocks = 0;
    while (done < totalFrames) {
        // Un micro-slip cada 7 bloques (MasterTiming quita 1 frame).
        int n = (blockIdx % 7 == 3) ? 319 : 320;
        if (done + n > totalFrames) n = totalFrames - done;
        int cap = n * 1002 / 1000 + 64;
        int produced = asrc.process(in.data() + done * 2, n, scratch.data(), cap);
        if (produced <= 0) ++zeroBlocks;
        out.insert(out.end(), scratch.begin(), scratch.begin() + produced * 2);
        done += n;
        ++blockIdx;
    }
    int outFrames = static_cast<int>(out.size() / 2);
    double ratio = static_cast<double>(outFrames) / totalFrames;
    std::printf("  bloques=%d (320/319), in=%d out=%d ratio=%.6f, bloques con 0 salida=%d\n",
                blockIdx, totalFrames, outFrames, ratio, zeroBlocks);
    check(zeroBlocks == 0, "ningún bloque produce 0 frames (el track nunca se queda sin datos)");
    check(std::fabs(ratio - 1.0) < 0.0002, "el número de frames sigue a la entrada (±0.02%)");
    bool allFinite = true;
    for (float v : out) if (!std::isfinite(v)) { allFinite = false; break; }
    check(allFinite, "salida finita con bloques variables");
    // Sin clicks: el mayor paso entre muestras consecutivas no supera
    // 1.5× la pendiente del seno (un slip quita 1 frame: el salto de fase
    // es ~ la pendiente del seno, no un click).
    double maxStep = 0.0;
    double sineStep = kAmp * 2.0 * kPi * kToneHz / kRate;
    for (int i = 0; i + 1 < outFrames; ++i) {
        double s = std::fabs(static_cast<double>(out[(i + 1) * 2]) - out[i * 2]);
        if (s > maxStep) maxStep = s;
    }
    std::printf("  max paso entre muestras = %.4f (pendiente seno = %.4f)\n",
                maxStep, sineStep);
    check(maxStep < sineStep * 1.5, "sin click con bloques variables");
}

void testCapturePsi() {
    std::printf("== (m) Captura: Ψ viva con bloques de 320 frames ==\n");
    // η se mueve con AM a 4 Hz (mismo criterio que (h), a 320 frames).
    {
        const int inFrames = 3 * kRate;
        std::vector<float> in;
        genAmTone(in, inFrames, kToneHz, 4.0);
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 320);
        (void)out;
        std::printf("  AM 4 Hz, 3 s, 320f: η = %.3f, coherencia = %.3f, activa = %s\n",
                    asrc.psiEta(), asrc.psiCoherence(),
                    asrc.psiActive() ? "SI" : "NO");
        check(asrc.psiActive(), "Ψ activa en captura tras el primer bloque");
        check(asrc.psiEta() > 0.6, "η sube con coherencia alta en captura (> 0.6)");
    }
    // ILD real a azimut 30° con tono mono (mismo criterio que (f)).
    {
        const int inFrames = 2 * kRate;
        std::vector<float> mono;
        genSine(mono, inFrames, kToneHz);
        for (int i = 0; i < inFrames; ++i) mono[i * 2 + 1] = mono[i * 2];
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        asrc.setPsiGeometry(-1.0f, -1.0f, 0.5236f);  // 30° a la derecha
        std::vector<float> out = processBlocked(asrc, mono, inFrames, 320);
        int outFrames = static_cast<int>(out.size() / 2);
        double rmsL = 0.0, rmsR = 0.0;
        for (int i = 2000; i < outFrames - 100; ++i) {
            rmsL += static_cast<double>(out[i * 2]) * out[i * 2];
            rmsR += static_cast<double>(out[i * 2 + 1]) * out[i * 2 + 1];
        }
        double ildDb = 10.0 * std::log10((rmsL + 1e-12) / (rmsR + 1e-12));
        std::printf("  azimut 30°, 320f: ILD de salida L/R = %.2f dB (mono de entrada)\n", ildDb);
        check(std::fabs(ildDb) > 0.5, "azimut 30° produce ILD real en captura (L != R)");
    }
    // Taps diseñados a 320 frames: finitos y acotados (mismo criterio que (g)).
    {
        const int inFrames = 2 * kRate;
        std::vector<float> in;
        genAmTone(in, inFrames, kToneHz, 4.0);
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
        std::vector<float> out = processBlocked(asrc, in, inFrames, 320);
        (void)out;
        float firL[64] = {}, firR[64] = {};
        check(asrc.psiFirTaps(firL, firR), "taps Ψ disponibles en captura tras procesar");
        double maxAbs = 0.0;
        bool allFinite = true;
        for (int i = 0; i < 64; ++i) {
            if (!std::isfinite(firL[i]) || !std::isfinite(firR[i])) allFinite = false;
            double a = std::fabs(static_cast<double>(firL[i]));
            double b = std::fabs(static_cast<double>(firR[i]));
            if (a > maxAbs) maxAbs = a;
            if (b > maxAbs) maxAbs = b;
        }
        std::printf("  max |tap| = %.4f (límite 4.0); finitos: %s\n", maxAbs,
                    allFinite ? "SI" : "NO");
        check(allFinite && maxAbs <= 4.0, "taps Ψ finitos y acotados en captura");
    }
}

void testCaptureCost() {
    std::printf("== (n) Captura: coste por bloque de 320 frames (ASRC+Ψ) ==\n");
    const int kBlocks = 1000;
    std::vector<float> in;
    genAmTone(in, 320, kToneHz, 4.0);  // contenido real (Ψ activa)
    std::vector<float> scratch(320 * 2 + 4096);
    auto measureMean = [&](RouteAAsrc& asrc, const char* label) {
        // Calentar 10 bloques (incluye el auto-diseño de Ψ del primer bloque).
        for (int i = 0; i < 10; ++i) {
            asrc.process(in.data(), 320, scratch.data(), 320 * 1002 / 1000 + 64);
        }
        std::vector<double> us;
        us.reserve(kBlocks);
        for (int i = 0; i < kBlocks; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            asrc.process(in.data(), 320, scratch.data(), 320 * 1002 / 1000 + 64);
            auto t1 = std::chrono::steady_clock::now();
            us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        std::sort(us.begin(), us.end());
        double sum = 0.0;
        for (double v : us) sum += v;
        double mean = sum / us.size();
        double p99 = us[static_cast<size_t>(us.size() * 0.99)];
        double mx = us.back();
        std::printf("  %s: media=%.1f µs p99=%.1f µs máx=%.1f µs\n", label, mean, p99, mx);
        return mean;
    };
    // Desglose: el ASRC solo (Ψ apagada) y la cadena completa (Ψ activa).
    // La 410 citaba ~40 µs/bloque en host; aquí se mide el proceso completo
    // (ASRC FIR 319 taps + FIR Ψ 64 taps/oído) en build escalar sin NEON.
    RouteAAsrc asrcOff;
    asrcOff.setClockCorrectionPpm(-70.0);  // deriva típica por USB
    asrcOff.setPsiActive(false);
    double meanOff = measureMean(asrcOff, "ASRC solo (Ψ apagada), -70 ppm");
    RouteAAsrc asrcOn;
    asrcOn.setClockCorrectionPpm(-70.0);
    double meanOn = measureMean(asrcOn, "ASRC+Ψ activa, -70 ppm");
    std::printf("  (presupuesto gobernador 85%% = 5670 µs; el ASRC debe caber en ~60 µs en el teléfono)\n");
    std::printf("  (host escalar sin NEON: el teléfono tiene NEON, el coste en vivo debe ser MENOR)\n");
    check(meanOff < 500.0, "coste medio ASRC solo < 500 µs en host");
    check(meanOn < 500.0, "coste medio ASRC+Ψ < 500 µs en host (< 9%% del presupuesto)");
}

}  // namespace

int main() {
    std::printf("ASRC magistral Ruta A — tests de host\n");
    std::printf("======================================\n");
    testRate();
    testThdn();
    testBlockContinuity();
    testFailClosed();
    testPsiFir();
    testPsiViva();
    testPsiTaps();
    testPsiEta();
    testPsiGroupDelay();
    testPsiThdn();
    testCaptureThdn();
    testCaptureVariableBlocks();
    testCapturePsi();
    testCaptureCost();
    std::printf("======================================\n");
    if (failures == 0) {
        std::printf("TODOS LOS TESTS PASARON\n");
        return 0;
    }
    std::printf("%d TEST(S) FALLARON\n", failures);
    return 1;
}
