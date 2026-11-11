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
//      ±1000 ppm; Ψ en bypass por defecto (sin cambios en la salida).
//  (e) FIR Ψ: convolución correcta (identidad y retardo de 32 muestras).
//
// Uso: g++ -std=c++17 -O2 -I<hal_probe_dir> ivanna_route_a_asrc.cpp
//      ivanna_route_a_asrc_test.cpp -o test && ./test
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
    {
        RouteAAsrc asrc;
        asrc.setClockCorrectionPpm(0.0);
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
        // Ψ en bypass por defecto: la salida es idéntica con Ψ apagada
        // que sin tocar Ψ (la puerta está cerrada desde el constructor).
        RouteAAsrc a1, a2;
        a1.setClockCorrectionPpm(0.0);
        a2.setClockCorrectionPpm(0.0);
        a2.setPsiActive(false);
        std::vector<float> o1 = processBlocked(a1, in, inFrames, 2048);
        std::vector<float> o2 = processBlocked(a2, in, inFrames, 2048);
        double maxDiff = 0.0;
        int n = static_cast<int>(o1.size() < o2.size() ? o1.size() : o2.size());
        for (int i = 0; i < n; ++i) {
            double d = std::fabs(static_cast<double>(o1[i]) - o2[i]);
            if (d > maxDiff) maxDiff = d;
        }
        std::printf("  Ψ bypass por defecto: max diff = %.3e\n", maxDiff);
        check(maxDiff == 0.0, "Ψ en bypass por defecto (salida idéntica)");
    }
}

void testPsiFir() {
    std::printf("== (e) FIR Ψ ==\n");
    const int inFrames = 2 * kRate;
    std::vector<float> in;
    genSine(in, inFrames, kToneHz);
    // Identidad: tap[0]=1, resto 0 → salida = entrada (tras el remuestreo).
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

}  // namespace

int main() {
    std::printf("ASRC magistral Ruta A — tests de host\n");
    std::printf("======================================\n");
    testRate();
    testThdn();
    testBlockContinuity();
    testFailClosed();
    testPsiFir();
    std::printf("======================================\n");
    if (failures == 0) {
        std::printf("TODOS LOS TESTS PASARON\n");
        return 0;
    }
    std::printf("%d TEST(S) FALLARON\n", failures);
    return 1;
}
