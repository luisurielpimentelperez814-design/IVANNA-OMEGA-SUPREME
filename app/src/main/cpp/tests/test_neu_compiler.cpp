// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// NEU·Ω Fase 2 — pruebas de equivalencia del compilador (E2/E3/E4/E6).
// Compara el SpectralCore + NeuCompiler contra los operadores seriales
// (ORBE/HOLOS) en dB RMS, según el contrato del §5 del diseño NEU·Ω.
// E2: ORBE solo ≤ −80 dB RMS. E3: HOLOS solo ≤ −80 dB RMS.
// E4: ORBE+HOLOS estacionario ≤ −60 dB RMS. E6: fades sin clicks.
#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

#include "spatial/NeuCompiler.hpp"
#include "spatial/OrbeBinaural.hpp"
#include "spatial/SpectralCore.hpp"

namespace {

constexpr float kSr = 48000.0f;
constexpr int kWarmup = 48000;   // 1 s para converger estimador + fades
constexpr int kWindow = 16384;   // ventana de comparación (estacionaria)

// Señal estacionaria mono (L=R): suma de senos en varias bandas ERB,
// para que el estimador de ORBE tenga energía en todas las bandas y
// la coherencia sea alta (puerta abierta, wet=1).
std::vector<float> MonoTones(int n) {
    std::vector<float> x((size_t)n);
    const float freqs[] = {100.0f, 300.0f, 700.0f, 1500.0f, 3000.0f,
                           6000.0f, 10000.0f, 14000.0f};
    for (int i = 0; i < n; ++i) {
        float s = 0.0f;
        for (float f : freqs)
            s += std::sin(2.0f * 3.14159265358979323846f * f * (float)i / kSr);
        x[(size_t)i] = 0.15f * s;
    }
    return x;
}

// RMS en dB de (a−b) relativo al RMS de a.
float RmsDiffDb(const std::vector<float>& a, const std::vector<float>& b,
                int lo, int hi) {
    double num = 0.0, den = 0.0;
    for (int i = lo; i < hi; ++i) {
        const double d = (double)a[(size_t)i] - (double)b[(size_t)i];
        num += d * d;
        den += (double)a[(size_t)i] * (double)a[(size_t)i];
    }
    if (den <= 0.0) return -200.0f;
    return (float)(10.0 * std::log10(num / den));
}

// Corre ORBE (in-place) sobre x (L=R) y devuelve la salida.
// El operador se calienta con kWarmup muestras y luego se captura
// la ventana kWindow.
void RunOrbe(ivanna::OrbeBinaural& orbe, const std::vector<float>& x,
             std::vector<float>& outL, std::vector<float>& outR) {
    const int total = kWarmup + kWindow;
    std::vector<float> l((size_t)total), r((size_t)total);
    for (int i = 0; i < total; ++i) {
        l[(size_t)i] = x[(size_t)(i % (int)x.size())];
        r[(size_t)i] = x[(size_t)(i % (int)x.size())];
    }
    // Procesar en bloques de 128 (el hop) para emular el vivo.
    for (int off = 0; off < total; off += 128) {
        const int n = std::min(128, total - off);
        orbe.process(l.data() + off, r.data() + off, n);
    }
    outL.assign(l.begin() + kWarmup, l.end());
    outR.assign(r.begin() + kWarmup, r.end());
}

}  // namespace

// E2: ORBE solo vía NEU·Ω (SpectralCore + compilador) vs ORBE serial.
// Se alimenta el compilador con el estado por banda que ORBE estimó
// (convergido, señal estacionaria) y se compara la salida en la
// ventana estacionaria. Umbral: ≤ −80 dB RMS.
TEST(NeuCompiler, E2OrbeSolo) {
    const auto x = MonoTones(kWarmup + kWindow);
    ivanna::OrbeBinaural orbe;
    orbe.prepare(kSr);
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    p.diffuse = 0.5f;
    p.proximityDb = 3.0f;
    p.distanceM = 0.35f;
    orbe.setParams(p);
    orbe.setAmount(1.0f);

    std::vector<float> refL, refR;
    RunOrbe(orbe, x, refL, refR);

    // Estado por banda convergido de ORBE → compilador.
    ivanna::neu::NeuCompiler comp;
    const int bands = ivanna::neu::SpectralCore::kMaxBands;
    for (int b = 0; b < bands; ++b) {
        comp.orbe.azimuth[b] = orbe.bandAzimuth(b);
        comp.orbe.distance[b] = orbe.bandDistance(b);
        comp.orbe.coherence[b] = orbe.bandCoherence(b);
    }
    comp.orbe.diffuse = 0.5f;
    comp.orbe.proximityDb = 3.0f;
    comp.orbe.headRadiusM = orbe.headRadiusM();
    comp.orbe.yawRad = 0.0f;

    // NEU·Ω: mismo segmento de entrada que la ventana de ORBE.
    ivanna::neu::SpectralCore core;
    core.prepare(kSr);
    core.setMatrixProvider(&ivanna::neu::NeuCompiler::provider, &comp);
    std::vector<float> inL(x.begin() + kWarmup, x.end());
    std::vector<float> inR = inL;
    std::vector<float> outL((size_t)kWindow), outR((size_t)kWindow);
    // Calentar el núcleo con el mismo warmup (para alinear la latencia
    // y el OLA), luego procesar la ventana.
    {
        std::vector<float> wL(x.begin(), x.begin() + kWarmup);
        std::vector<float> wR = wL;
        std::vector<float> tmpL((size_t)kWarmup), tmpR((size_t)kWarmup);
        core.process(wL.data(), wR.data(), tmpL.data(), tmpR.data(), kWarmup);
    }
    core.process(inL.data(), inR.data(), outL.data(), outR.data(), kWindow);

    // Comparar en la zona estacionaria (tras la latencia de 512).
    // ORBE y el núcleo tienen la misma latencia (512), así que las
    // salidas se alinean muestra a muestra en la ventana.
    const int lo = 1024, hi = kWindow - 512;
    const float dbL = RmsDiffDb(refL, outL, lo, hi);
    const float dbR = RmsDiffDb(refR, outR, lo, hi);
    EXPECT_LE(dbL, -80.0f) << "E2 ORBE solo (L): " << dbL << " dB";
    EXPECT_LE(dbR, -80.0f) << "E2 ORBE solo (R): " << dbR << " dB";
}
