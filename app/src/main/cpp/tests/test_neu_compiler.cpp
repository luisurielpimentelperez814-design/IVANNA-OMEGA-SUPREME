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

#include "spatial/HolosOperator.hpp"
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
        x[(size_t)i] = 0.05f * s;
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

// RMS en dB con desplazamiento: compara a[i] contra b[i+shift].
// Para E4 (la cadena serial va 512 muestras por detrás de NEU·Ω).
float RmsDiffDbShift(const std::vector<float>& a, const std::vector<float>& b,
                     int lo, int hi, int shift) {
    double num = 0.0, den = 0.0;
    for (int i = lo; i < hi; ++i) {
        const double d = (double)a[(size_t)i] - (double)b[(size_t)(i + shift)];
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

// Corre HOLOS (in-place) sobre x (L=R) y devuelve la salida.
void RunHolos(ivanna::HolosOperator& holos, const std::vector<float>& x,
              std::vector<float>& outL, std::vector<float>& outR) {
    const int total = kWarmup + kWindow;
    std::vector<float> l((size_t)total), r((size_t)total);
    for (int i = 0; i < total; ++i) {
        l[(size_t)i] = x[(size_t)(i % (int)x.size())];
        r[(size_t)i] = x[(size_t)(i % (int)x.size())];
    }
    for (int off = 0; off < total; off += 128) {
        const int n = std::min(128, total - off);
        holos.process(l.data() + off, r.data() + off, n);
    }
    outL.assign(l.begin() + kWarmup, l.end());
    outR.assign(r.begin() + kWarmup, r.end());
}

// Llena el estado HOLOS del compilador desde un HolosOperator convergido.
void FillHolosState(ivanna::HolosOperator& holos, ivanna::neu::NeuCompiler& comp,
                    float gamma, float proximityDb) {
    const int bands = ivanna::neu::SpectralCore::kMaxBands;
    for (int b = 0; b < bands; ++b) {
        const auto& s = holos.bandState(b);
        comp.holos.theta[b] = s.theta;
        comp.holos.distance[b] = s.r;
        comp.holos.gammaLin[b] =
            std::pow(10.0f, holos.bandGammaDb(b) / 20.0f);
        comp.holos.wSide[b] = holos.bandWidthGain(b);
        comp.holos.diffuseMix[b] = holos.bandDiffuseMix(b);
    }
    comp.holos.gamma = gamma;
    comp.holos.proximityDb = proximityDb;
    comp.holos.yawRad = 0.0f;
    comp.holos.pinnaDb = 0.0f;  // sin tabla cargada
}

// E3: HOLOS solo vía NEU·Ω vs HOLOS serial (solo la parte directa;
// la cola difusa es Fase 3). Señal mono estacionaria → coherencia
// alta → diffuseMix ≈ 0 → la cola no aporta. Umbral: ≤ −80 dB RMS.
TEST(NeuCompiler, E3HolosSolo) {
    const auto x = MonoTones(kWarmup + kWindow);
    ivanna::HolosOperator holos;
    holos.prepare(kSr);
    ivanna::HolosParams p;
    p.amount = 1.0f;
    p.diffuse = 0.5f;
    p.proximityDb = 3.0f;
    p.distanceM = 0.35f;
    p.gamma = 1.0f;
    p.harmonics = 0.0f;  // Volterra no lineal: identidad (Fase 2 = parte lineal)
    holos.setParams(p);
    holos.setAmount(1.0f);

    std::vector<float> refL, refR;
    RunHolos(holos, x, refL, refR);

    ivanna::neu::NeuCompiler comp;
    comp.useOrbe = false;
    comp.useHolos = true;
    FillHolosState(holos, comp, 1.0f, 3.0f);

    ivanna::neu::SpectralCore core;
    core.prepare(kSr);
    core.setMatrixProvider(&ivanna::neu::NeuCompiler::provider, &comp);
    std::vector<float> inL(x.begin() + kWarmup, x.end());
    std::vector<float> inR = inL;
    std::vector<float> outL((size_t)kWindow), outR((size_t)kWindow);
    {
        std::vector<float> wL(x.begin(), x.begin() + kWarmup);
        std::vector<float> wR = wL;
        std::vector<float> tmpL((size_t)kWarmup), tmpR((size_t)kWarmup);
        core.process(wL.data(), wR.data(), tmpL.data(), tmpR.data(), kWarmup);
    }
    core.process(inL.data(), inR.data(), outL.data(), outR.data(), kWindow);

    const int lo = 1024, hi = kWindow - 512;
    const float dbL = RmsDiffDb(refL, outL, lo, hi);
    const float dbR = RmsDiffDb(refR, outR, lo, hi);
    EXPECT_LE(dbL, -80.0f) << "E3 HOLOS solo (L): " << dbL << " dB";
    EXPECT_LE(dbR, -80.0f) << "E3 HOLOS solo (R): " << dbR << " dB";
}

// E4: ORBE+HOLOS vía NEU·Ω (A_total = A_HOLOS·A_ORBE) vs la cadena
// serial ORBE→HOLOS. La cadena serial tiene latencia 1024 (512+512)
// y NEU·Ω 512, así que la salida serial va 512 muestras por detrás:
// se compara serial[i] contra neu[i+512]. Umbral: ≤ −60 dB RMS.
TEST(NeuCompiler, E4OrbeHolos) {
    const auto x = MonoTones(kWarmup + kWindow);
    // ORBE serial.
    ivanna::OrbeBinaural orbe;
    orbe.prepare(kSr);
    ivanna::OrbeParams op;
    op.amount = 1.0f; op.diffuse = 0.5f; op.proximityDb = 3.0f; op.distanceM = 0.35f;
    orbe.setParams(op);
    orbe.setAmount(1.0f);
    // HOLOS serial (recibe la salida de ORBE).
    ivanna::HolosOperator holos;
    holos.prepare(kSr);
    ivanna::HolosParams hp;
    hp.amount = 1.0f; hp.diffuse = 0.5f; hp.proximityDb = 3.0f;
    hp.distanceM = 0.35f; hp.gamma = 1.0f; hp.harmonics = 0.0f;
    holos.setParams(hp);
    holos.setAmount(1.0f);

    const int total = kWarmup + kWindow;
    std::vector<float> l((size_t)total), r((size_t)total);
    for (int i = 0; i < total; ++i) {
        l[(size_t)i] = x[(size_t)(i % (int)x.size())];
        r[(size_t)i] = x[(size_t)(i % (int)x.size())];
    }
    for (int off = 0; off < total; off += 128) {
        const int n = std::min(128, total - off);
        orbe.process(l.data() + off, r.data() + off, n);
        holos.process(l.data() + off, r.data() + off, n);
    }
    std::vector<float> refL(l.begin() + kWarmup, l.end());
    std::vector<float> refR(r.begin() + kWarmup, r.end());

    // Compilador con ambos estados convergidos.
    ivanna::neu::NeuCompiler comp;
    comp.useOrbe = true;
    comp.useHolos = true;
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
    FillHolosState(holos, comp, 1.0f, 3.0f);

    ivanna::neu::SpectralCore core;
    core.prepare(kSr);
    core.setMatrixProvider(&ivanna::neu::NeuCompiler::provider, &comp);
    std::vector<float> inL(x.begin() + kWarmup, x.end());
    std::vector<float> inR = inL;
    std::vector<float> outL((size_t)kWindow), outR((size_t)kWindow);
    {
        std::vector<float> wL(x.begin(), x.begin() + kWarmup);
        std::vector<float> wR = wL;
        std::vector<float> tmpL((size_t)kWarmup), tmpR((size_t)kWarmup);
        core.process(wL.data(), wR.data(), tmpL.data(), tmpR.data(), kWarmup);
    }
    core.process(inL.data(), inR.data(), outL.data(), outR.data(), kWindow);

    // Serial va 512 por detrás: serial[i+512] ≈ neu[i]
    // (serial[t]=entrada[t−1024], neu[t]=entrada[t−512]).
    // Se compara neu[i] contra serial[i+512].
    const int lo = 1024, hi = kWindow - 1024;
    const float dbL = RmsDiffDbShift(outL, refL, lo, hi, 512);
    const float dbR = RmsDiffDbShift(outR, refR, lo, hi, 512);
    EXPECT_LE(dbL, -60.0f) << "E4 ORBE+HOLOS (L): " << dbL << " dB";
    EXPECT_LE(dbR, -60.0f) << "E4 ORBE+HOLOS (R): " << dbR << " dB";
}

// E6: fades sin discontinuidad ni zipper (P4: M(w) = I + w·(M−I)).
// Con un estado ORBE fijo: fade=0 → identidad, fade=1 → matriz
// completa, fade=0.5 → punto medio exacto. Y al rampar el fade de 0
// a 1 en 100 hops, la salida no tiene saltos (sin clicks).
TEST(NeuCompiler, E6Fades) {
    const auto x = MonoTones(kWindow);
    // Estado ORBE fijo (determinista): fuente a 0.3 rad, 0.35 m.
    ivanna::neu::NeuCompiler comp;
    comp.useOrbe = true;
    comp.useHolos = false;
    const int bands = ivanna::neu::SpectralCore::kMaxBands;
    for (int b = 0; b < bands; ++b) {
        comp.orbe.azimuth[b] = 0.3f;
        comp.orbe.distance[b] = 0.35f;
        comp.orbe.coherence[b] = 1.0f;
    }
    comp.orbe.diffuse = 0.5f;
    comp.orbe.proximityDb = 3.0f;
    comp.orbe.yawRad = 0.0f;

    auto runWithFade = [&](float fade) {
        ivanna::neu::SpectralCore core;
        core.prepare(kSr);
        comp.fade = fade;
        core.setMatrixProvider(&ivanna::neu::NeuCompiler::provider, &comp);
        std::vector<float> inL = x, inR = x;
        std::vector<float> outL((size_t)kWindow), outR((size_t)kWindow);
        core.process(inL.data(), inR.data(), outL.data(), outR.data(), kWindow);
        return std::make_pair(outL, outR);
    };

    const auto out0 = runWithFade(0.0f);   // identidad
    const auto out1 = runWithFade(1.0f);   // matriz completa
    const auto outH = runWithFade(0.5f);   // punto medio
    const int lo = 1024, hi = kWindow - 512;
    // fade=0 → salida = entrada (identidad).
    EXPECT_LE(RmsDiffDb(x, out0.first, lo, hi), -80.0f)
        << "E6 fade=0 debe ser identidad";
    // fade=0.5 → punto medio entre identidad y matriz completa.
    {
        double num = 0.0, den = 0.0;
        for (int i = lo; i < hi; ++i) {
            const double expect =
                0.5 * (double)x[(size_t)i] + 0.5 * (double)out1.first[(size_t)i];
            const double d = (double)outH.first[(size_t)i] - expect;
            num += d * d;
            den += expect * expect;
        }
        const float db = (float)(10.0 * std::log10(num / den));
        EXPECT_LE(db, -80.0f) << "E6 fade=0.5 debe ser punto medio: " << db << " dB";
    }
    // Rampa 0→1 en 100 hops: sin saltos (sin clicks).
    {
        ivanna::neu::SpectralCore core;
        core.prepare(kSr);
        core.setMatrixProvider(&ivanna::neu::NeuCompiler::provider, &comp);
        const int nRamp = 100 * 128;
        std::vector<float> inL(x.begin(), x.begin() + nRamp);
        std::vector<float> inR = inL;
        std::vector<float> outL((size_t)nRamp), outR((size_t)nRamp);
        // Rampar el fade por hop (0→1 en 100 hops).
        for (int h = 0; h < 100; ++h) {
            comp.fade = (float)h / 99.0f;
            core.process(inL.data() + h * 128, inR.data() + h * 128,
                         outL.data() + h * 128, outR.data() + h * 128, 128);
        }
        // Máximo salto muestra a muestra (tras la latencia).
        float maxJump = 0.0f;
        for (int i = 1025; i < nRamp; ++i)
            maxJump = std::max(maxJump, std::fabs(outL[(size_t)i] - outL[(size_t)(i - 1)]));
        // La señal tiene agudos (14 kHz) y su propia pendiente natural
        // es grande; un click sería un salto MUCHO mayor que esa
        // pendiente. Umbral: 3× la pendiente máxima de la entrada.
        float maxInJump = 0.0f;
        for (int i = 1; i < nRamp; ++i)
            maxInJump = std::max(maxInJump, std::fabs(inL[(size_t)i] - inL[(size_t)(i - 1)]));
        EXPECT_LE(maxJump, 3.0f * maxInJump)
            << "E6 rampa: salto max " << maxJump << " vs entrada " << maxInJump;
    }
}
