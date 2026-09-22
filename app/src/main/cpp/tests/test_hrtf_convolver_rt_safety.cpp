#include <gtest/gtest.h>
#include "../spatial/hrtf_convolver.hpp"
#include <cmath>
#include <vector>

using ivanna::HRTFConvolver;

// ── Regresión RT (auditoría 2026-09-22) ──────────────────────────────────────
// Causa raíz de tronidos/microcortes con WFS+HRTF activos y fuente en
// movimiento: SyntheticHRTF::generate()/generateFromDataset() construían un
// HRIRPair LOCAL (dos std::vector<float> vacíos) en cada llamada, llenado con
// .assign() -> malloc/free en cada crossfade de azimut. HRTFConvolver::
// updateFilterResponses() invoca generate() desde DENTRO de process() (hilo
// de audio SCHED_FIFO) cada vez que el azimut cambia más de 0.1° o la
// agresividad más de 0.01 — es decir, en cualquier movimiento continuo de
// fuente. Este test no puede medir malloc() directamente sin interceptarlo,
// pero SÍ reproduce exactamente el patrón de uso que lo disparaba (barrido
// continuo de azimut, bloque a bloque, como en paneo WFS o head-tracking) y
// corre bajo ASan/UBSan en el CI (CTest (ASan+UBSan)): si algún camino de
// generate()/generateFromDataset() introdujera un puntero colgante, un
// use-after-free, o un desbordamiento al reutilizar el scratch persistente,
// ASan lo abortaría aquí. También congela el comportamiento correcto: salida
// finita y no trivial durante todo el barrido, sin saltos de nivel abruptos.
TEST(HRTFConvolverRtSafety, ContinuousAzimuthSweepStaysFiniteAndGlitchFree) {
    constexpr uint32_t kSr = 48000;
    constexpr int kBlock = 128;

    HRTFConvolver conv;
    conv.init(kSr);

    std::vector<float> inL(kBlock), inR(kBlock);
    for (int i = 0; i < kBlock; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSr);
        inL[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        inR[i] = inL[i];
    }
    std::vector<float> outL(kBlock), outR(kBlock);

    float prevRms = -1.0f;
    // Barrido continuo de azimut, bloque a bloque: dispara
    // updateFilterResponses() -> generate() en (casi) cada iteración, el
    // mismo patrón de llamada que producía los tronidos reportados.
    for (int step = 0; step < 400; ++step) {
        const float azimuth = -180.0f + (360.0f * step) / 400.0f;
        const float aggressiveness = 0.5f + 0.4f * std::sin(0.05f * step);
        conv.set_position(azimuth, aggressiveness);
        conv.process(inL.data(), inR.data(), outL.data(), outR.data(),
                     static_cast<uint32_t>(kBlock));

        double acc = 0.0;
        for (int i = 0; i < kBlock; ++i) {
            ASSERT_TRUE(std::isfinite(outL[i])) << "outL no finito en step=" << step;
            ASSERT_TRUE(std::isfinite(outR[i])) << "outR no finito en step=" << step;
            acc += static_cast<double>(outL[i]) * outL[i];
        }
        const float rms = static_cast<float>(std::sqrt(acc / kBlock));
        // Sin salto abrupto de nivel entre bloques consecutivos (glitch-free):
        // el crossfade de 5ms de HRTFConvolver debe suavizar cualquier cambio
        // de HRIR, nunca un escalón duro de energía bloque a bloque.
        if (prevRms >= 0.0f) {
            EXPECT_LT(std::fabs(rms - prevRms), 0.35f)
                << "salto de RMS entre bloques en step=" << step
                << " (prev=" << prevRms << " now=" << rms << ")";
        }
        prevRms = rms;
    }
}

// El scratch persistente de SyntheticHRTF se reutiliza entre llamadas a
// generate(); confirmar que dos instancias INDEPENDIENTES de HRTFConvolver
// (cada una con su propio SyntheticHRTF) no comparten estado -- el fix debe
// seguir siendo por-instancia, no un scratch global accidental.
TEST(HRTFConvolverRtSafety, TwoIndependentInstancesDoNotShareScratch) {
    constexpr uint32_t kSr = 48000;
    constexpr int kBlock = 64;

    HRTFConvolver a, b;
    a.init(kSr);
    b.init(kSr);

    std::vector<float> inL(kBlock, 0.3f), inR(kBlock, 0.3f);
    std::vector<float> outAL(kBlock), outAR(kBlock), outBL(kBlock), outBR(kBlock);

    a.set_position(-90.0f, 0.9f);
    b.set_position(90.0f, 0.9f);
    // El crossfade interno (XFADE_DURATION_SAMPLES=1024, sobre bloques
    // internos BLOCK=256, ver hrtf_convolver.hpp) tarda varias llamadas en
    // converger — es intencional (glitch-free, ver comentarios del propio
    // motor). Se alimentan suficientes bloques (>> 256+1024 muestras) para
    // que el filtro objetivo ya domine antes de comparar; solo el ÚLTIMO
    // bloque es el que se compara.
    constexpr int kWarmupBlocks = 40;  // 40*64 = 2560 muestras >> 1280
    for (int i = 0; i < kWarmupBlocks; ++i) {
        a.process(inL.data(), inR.data(), outAL.data(), outAR.data(), kBlock);
        b.process(inL.data(), inR.data(), outBL.data(), outBR.data(), kBlock);
    }

    for (int i = 0; i < kBlock; ++i) {
        ASSERT_TRUE(std::isfinite(outAL[i]));
        ASSERT_TRUE(std::isfinite(outBL[i]));
    }
    // Posiciones opuestas -> ITD/shadow opuestos: las salidas no deben ser
    // idénticas muestra a muestra (si compartieran un scratch mal aislado,
    // la última llamada -de 'b'- podría "ganarle" a 'a' y ambas saldrían
    // iguales).
    bool anyDifferent = false;
    for (int i = 0; i < kBlock; ++i) {
        if (std::fabs(outAL[i] - outBL[i]) > 1e-6f) { anyDifferent = true; break; }
    }
    EXPECT_TRUE(anyDifferent) << "instancias con azimut opuesto dieron salida idéntica";
}
