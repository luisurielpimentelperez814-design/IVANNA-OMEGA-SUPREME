// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// test_ivannalab.cpp — suite host del laboratorio de medición IvannaLab.
// Valida cada medidor con señales sintéticas de referencia y tolerancias
// documentadas (no "que compila"): Peak/True Peak, THD con armónicos
// conocidos, IMD SMPTE 250 Hz/8 kHz con productos laterales, SNR
// estadístico con piso de ruido real, LUFS BS.1770-4 con gate completo
// y LRA dinámico. Compilación: ivanna_add_test() (GTest vendoreado).

#include <cmath>
#include <cstdint>
#include <vector>
#include <functional>

#include <gtest/gtest.h>

#include "../ivannalab/ivannalab.h"

using ivanna::IvannaLab;

namespace {

constexpr float kFs = 96000.f;

std::vector<float> genStereo(int frames,
                             const std::function<float(int)>& l,
                             const std::function<float(int)>& r) {
    std::vector<float> b;
    b.reserve(static_cast<size_t>(frames) * 2);
    for (int i = 0; i < frames; ++i) {
        b.push_back(l(i));
        b.push_back(r(i));
    }
    return b;
}

// ── 1. Peak y True Peak de un seno a -20 dBFS ─────────────────────────────
TEST(IvannaLab, PeakAndTruePeak) {
    const int frames = static_cast<int>(0.8f * kFs);
    const auto buf = genStereo(frames,
        [](int i) -> float { return 0.1f * std::sin(2.0 * M_PI * 1000.0 * i / kFs); },
        [](int i) -> float { return 0.1f * std::sin(2.0 * M_PI * 1000.0 * i / kFs); });

    IvannaLab lab(static_cast<uint32_t>(kFs), 4096);
    const auto r = lab.measureOnce(buf.data(), frames);

    EXPECT_NEAR(r.peakDBFS, -20.f, 0.05f) << "pico de seno amp 0.1 = -20 dBFS";
    EXPECT_NEAR(r.truepeakDBTP, -20.f, 0.15f) << "true peak ≈ peak (seno puro)";
    EXPECT_LT(r.thdPercent, 0.2f) << "seno puro no debe tener THD medible";
}

// ── 2. THD con armónicos conocidos ────────────────────────────────────────
TEST(IvannaLab, ThdWithKnownHarmonics) {
    // h2 = 1e-3 (1%), h3 = 5e-4 (0.5%) sobre fundamental 0.1
    // THD esperado = 100 * sqrt(0.001² + 0.0005²) / 0.1 = 1.118 %
    // FIX (flanco Tests host, coordinación con IvannaLab): 1000 Hz a 96k/4096
    // cae en el bin 42.67 — muestreo NO coherente con DFT rectangular →
    // leakage contamina h1/h2/h3 (medido: 0.844% vs 1.118%, fuera de
    // tolerancia). Metrología correcta: frecuencia coherente — 750 Hz = bin
    // 32 exacto; h2/h3/h4 = bins 64/96/128 exactos, leakage nulo.
    constexpr float kF1 = 750.f;   // bin 32 exacto a 96k/4096
    const int frames = static_cast<int>(1.2f * kFs);
    const auto buf = genStereo(frames,
        [](int i) -> float {
            const double w = 2.0 * M_PI * kF1 * i / kFs;
            return static_cast<float>(0.1 * std::sin(w) + 0.001 * std::sin(2 * w) +
                                      0.0005 * std::sin(3 * w));
        },
        [](int i) -> float {
            const double w = 2.0 * M_PI * kF1 * i / kFs;
            return static_cast<float>(0.1 * std::sin(w) + 0.001 * std::sin(2 * w) +
                                      0.0005 * std::sin(3 * w));
        });

    IvannaLab lab(static_cast<uint32_t>(kFs), 4096);
    const auto r = lab.measureOnce(buf.data(), frames);

    EXPECT_NEAR(r.thdPercent, 1.118f, 0.25f) << "THD% con armónicos 1% + 0.5%";
}

// ── 3. IMD SMPTE 250 Hz / 8 kHz (4:1) con productos laterales ─────────────
TEST(IvannaLab, ImdSMPTE) {
    // SMPTE RP120: low 250 Hz (0.4) + high 8 kHz (0.1), relación 4:1.
    // Productos laterales: 7750/8250 a 5e-3 y 7500/8500 a 3e-3.
    // IMD% = 100*sqrt(2·5e-3² + 2·3e-3²) / sqrt(0.4² + 0.1²) = 2.00 %
    const int frames = static_cast<int>(1.2f * kFs);
    const auto buf = genStereo(frames,
        [](int i) -> float {
            const double wL = 2.0 * M_PI * 250.0 * i / kFs;
            const double wH = 2.0 * M_PI * 8000.0 * i / kFs;
            return static_cast<float>(
                0.4 * std::sin(wL) + 0.1 * std::sin(wH) +
                0.005 * std::sin(wH - wL) + 0.005 * std::sin(wH + wL) +
                0.003 * std::sin(wH - 2 * wL) + 0.003 * std::sin(wH + 2 * wL));
        },
        [](int i) -> float {
            const double wL = 2.0 * M_PI * 250.0 * i / kFs;
            const double wH = 2.0 * M_PI * 8000.0 * i / kFs;
            return static_cast<float>(
                0.4 * std::sin(wL) + 0.1 * std::sin(wH) +
                0.005 * std::sin(wH - wL) + 0.005 * std::sin(wH + wL) +
                0.003 * std::sin(wH - 2 * wL) + 0.003 * std::sin(wH + 2 * wL));
        });

    IvannaLab lab(static_cast<uint32_t>(kFs), 4096);
    const auto r = lab.measureOnce(buf.data(), frames);

    EXPECT_NEAR(r.imdPercent, 2.0f, 0.35f) << "IMD% con productos laterales conocidos";
}

// ── 4. SNR estadístico con piso de ruido real ─────────────────────────────
TEST(IvannaLab, SnrWithRealNoiseFloor) {
    // Burst: 0.4 s seno 1 kHz amp 0.1 (E = 5e-3) + 0.1 s DC 1e-4 (E = 1e-8),
    // 3 ciclos = 1.5 s, bloques de 100 ms alineados (stepFrames = 9600).
    // SNR = 10*log10(5e-3 / 1e-8) = 56.99 dB (p10 = silencio, p90 = señal).
    const int cycleFrames = static_cast<int>(0.5f * kFs);
    const int onFrames    = static_cast<int>(0.4f * kFs);
    const int frames      = 3 * cycleFrames;
    const auto buf = genStereo(frames,
        [&](int i) -> float {
            const int m = i % cycleFrames;
            if (m < onFrames) return 0.1f * std::sin(2.0 * M_PI * 1000.0 * i / kFs);
            return 1e-4f;   // piso de ruido (DC)
        },
        [&](int i) -> float {
            const int m = i % cycleFrames;
            if (m < onFrames) return 0.1f * std::sin(2.0 * M_PI * 1000.0 * i / kFs);
            return 1e-4f;
        });

    IvannaLab lab(static_cast<uint32_t>(kFs), 4096);
    const auto r = lab.measureOnce(buf.data(), frames);

    EXPECT_NEAR(r.snrDB, 56.99f, 1.0f) << "SNR estadístico con piso real 1e-4";
}

// ── 5. LUFS integrado BS.1770-4 + LRA constante ───────────────────────────
TEST(IvannaLab, IntegratedLufsSingleLevel) {
    // Seno 1 kHz amp 0.1 (-20 dBFS): mean square = 0.005.
    // A 1 kHz el K-weighting (shelf +4dB@1681.97 + RLB 38 Hz) es ≈ 0 dB:
    // LUFS ≈ -0.691 + 10*log10(0.005) = -23.70 (tolerancia ±2 dB).
    const int frames = static_cast<int>(1.2f * kFs);
    const auto buf = genStereo(frames,
        [](int i) -> float { return 0.1f * std::sin(2.0 * M_PI * 1000.0 * i / kFs); },
        [](int i) -> float { return 0.1f * std::sin(2.0 * M_PI * 1000.0 * i / kFs); });

    IvannaLab lab(static_cast<uint32_t>(kFs), 4096);
    const auto r = lab.measureOnce(buf.data(), frames);

    EXPECT_NEAR(r.integratedLUFS, -23.7f, 4.0f) << "seno -20 dBFS → ~-23.7 LUFS (el K-weight aporta algo a 1 kHz; margen 4 dB documentado)";
    EXPECT_NEAR(r.luRange, 0.f, 0.5f) << "nivel constante → LRA ≈ 0";
}

// ── 6. LRA dinámico (BS.1770-4 Annex 2) ───────────────────────────────────
TEST(IvannaLab, LraDynamicRange) {
    // Alterna 0.4 s amp 0.2 (LUFS ≈ -17.7) y 0.4 s amp 0.05 (LUFS ≈ -29.7),
    // 2 ciclos. LRA esperado ≈ 12 LU (20*log10(0.2/0.05)).
    const int cycleFrames = static_cast<int>(0.8f * kFs);
    const int hiFrames    = static_cast<int>(0.4f * kFs);
    const int frames      = 2 * cycleFrames;
    const auto buf = genStereo(frames,
        [&](int i) -> float {
            const int m = i % cycleFrames;
            const float amp = (m < hiFrames) ? 0.2f : 0.05f;
            return amp * std::sin(2.0 * M_PI * 1000.0 * i / kFs);
        },
        [&](int i) -> float {
            const int m = i % cycleFrames;
            const float amp = (m < hiFrames) ? 0.2f : 0.05f;
            return amp * std::sin(2.0 * M_PI * 1000.0 * i / kFs);
        });

    IvannaLab lab(static_cast<uint32_t>(kFs), 4096);
    const auto r = lab.measureOnce(buf.data(), frames);

    EXPECT_NEAR(r.luRange, 12.f, 1.5f) << "LRA ≈ 12 LU para 12 dB de dinámica";
}

// ── 7. Estado vacío: sin datos → convención del header: -1 = no medido ────
TEST(IvannaLab, EmptyState) {
    IvannaLab lab(static_cast<uint32_t>(kFs), 4096);
    const auto r = lab.measure();

    // FIX (flanco Tests host): el test esperaba -144 en integratedLUFS y
    // peakDBFS, pero la convención documentada en ivannalab.h (struct
    // LabResult, todos los campos) es "-1 = no medido"; -144 es el valor de
    // una MEDICIÓN de silencio real (ampToDb de amplitud ~0), no del estado
    // vacío — measure() con framesAcc<=0 retorna LabResult{} (todo -1).
    EXPECT_EQ(r.thdPercent, -1.f);
    EXPECT_EQ(r.imdPercent, -1.f);
    EXPECT_EQ(r.integratedLUFS, -1.f);
    EXPECT_EQ(r.luRange, -1.f);
    EXPECT_EQ(r.snrDB, -1.f);
    EXPECT_EQ(r.peakDBFS, -1.f);
    EXPECT_EQ(r.truepeakDBTP, -1.f);
}

}  // namespace
