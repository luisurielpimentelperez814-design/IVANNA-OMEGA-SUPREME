#pragma once
//
// ivanna_stage_timings.h — Tiempos por etapa del DSP, Ruta A (v2.5.411, tarea 4).
//
// Problema: la telemetría 1 Hz (IVANNA.Telemetry) llevaba el coste TOTAL del
// DSP (shed/ema/peak del gobernador) pero no decía QUÉ etapa se come el
// bloque — y Esse mide underruns +6 / overruns +10 por ~90 s con CPU
// oscilando 64–163%. El AudioThreadBudgetGuard (Kotlin) solo tiene 5
// etapas gruesas del lado Kotlin (DSP_BRIDGE = toda la llamada JNI) y
// media móvil, sin p99 ni máximo. Hacía falta el desglose nativo.
//
// Diseño (lock-free, cero alloc, cero locks en el hilo RT):
//   - 7 etapas (las de la cadena viva de nativeProcessBlock + ASRC):
//     Hoa (PDEngine NHO+Spatial + WFS, base HOA/HRTF), Orbe, Holos, Phi
//     (hyperProcessRouteA), Volterra, Npe (cochlear + pipe unificado) y
//     Asrc (RouteAAsrc::process, hilo del decode loop del BridgePlayer).
//     OmniField NO está: en nativeProcessBlock no se llama (bypass 410).
//   - Por etapa: suma + contador (media), máximo (CAS loop) y anillo de
//     256 muestras (p99 exacto de la ventana). Todo atómicos relaxed: el
//     hilo RT solo hace stores; el sort lo hace el getter en el hilo del
//     poller 1 Hz, nunca en RT.
//   - Ventana de 1 s: el getter (nativeGetDspShed, extendido) hace
//     snapshot-and-reset en cada lectura; el poller lee 1 Hz, así que cada
//     línea cubre ~el último segundo. Una muestra que caiga justo en el
//     reset puede contarse en una ventana u otra: irrelevante para
//     telemetría (los valores son aproximados por naturaleza).
//   - blockBudgetNs: periodo del último bloque procesado (ns) — el
//     presupuesto contra el que se compara cada etapa. Lo escribe
//     nativeProcessBlock; el ASRC no lo toca (su periodo es el del decode,
//     no el del bloque DSP).
//   - Todo cero = la etapa no corrió en la ventana (bypass por amount=0
//     o sin bloques). No confundir con "etapa instantánea".
//   - La instancia vive en ivanna_route_a_asrc.cpp (NO en el JNI): el test
//     standalone del ASRC solo enlaza ese TU y el .so lo comparte con el
//     JNI — una sola definición en ambos builds (regla: todo extern se
//     DEFINE en un .cpp del MISMO target).
//   - Sin funciones nuevas en la API (ni JNI ni Kotlin): el transporte es
//     el getter existente nativeGetDspShed (FloatArray extendido de 3 a 25:
//     [0..2] shed como antes, [3] budgetNs, [4..24] mean/p99/max por etapa).
//     stageRecord/stageSnapshotReset son helpers inline de este header,
//     no API nueva.
//
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace ivanna {

enum class Stage : int {
    Hoa = 0,   // PDEngine (NHO + Spatial) + WFS — base HOA/HRTF
    Orbe = 1,  // g_orbeRouteA.process
    Holos = 2, // g_holosRouteA.process
    Phi = 3,   // hyperProcessRouteA (g_hyperRouteA.processBlock)
    Volterra = 4, // g_volterra_engine.processInterleaved
    Npe = 5,   // g_cochlearEngine + pipe (snnNmfHoaUpmixer, pinna, transharmonic, shmMso, warpedLattice)
    Asrc = 6,  // RouteAAsrc::process (hilo decode loop BridgePlayer)
    Count = 7
};
constexpr int kStageCount = 7;
constexpr int kStageRingSize = 256;

struct StageTimings {
    std::atomic<uint64_t> sumNs[kStageCount]{};
    std::atomic<uint64_t> count[kStageCount]{};
    std::atomic<uint32_t> maxNs[kStageCount]{};
    std::atomic<uint32_t> ring[kStageCount][kStageRingSize]{};
    std::atomic<uint32_t> ringIdx[kStageCount]{};
    std::atomic<uint64_t> blockBudgetNs{0};
};

extern StageTimings g_stageTimings;

// Registra una muestra (hilo RT): solo stores atómicos relaxed, sin alloc,
// sin locks, sin I/O. ns se clampa a uint32 (~4.29 s: un bloque que tarde
// más ya es shed/overrun de todas formas).
inline void stageRecord(Stage s, uint64_t ns) noexcept {
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kStageCount) return;
    const uint32_t v = (ns > 0xFFFFFFFFull) ? 0xFFFFFFFFu : static_cast<uint32_t>(ns);
    g_stageTimings.sumNs[i].fetch_add(ns, std::memory_order_relaxed);
    g_stageTimings.count[i].fetch_add(1, std::memory_order_relaxed);
    uint32_t prev = g_stageTimings.maxNs[i].load(std::memory_order_relaxed);
    while (v > prev &&
           !g_stageTimings.maxNs[i].compare_exchange_weak(prev, v, std::memory_order_relaxed)) {
    }
    const uint32_t idx = g_stageTimings.ringIdx[i].fetch_add(1, std::memory_order_relaxed);
    g_stageTimings.ring[i][idx % kStageRingSize].store(v, std::memory_order_relaxed);
}

// Snapshot de la ventana + reset (hilo del poller 1 Hz, NUNCA el hilo RT).
// out: kStageCount*3 floats [mean_ns, p99_ns, max_ns] por etapa, en orden
// de Stage. El sort de <=256 muestras por etapa corre aquí, en el poller.
inline void stageSnapshotReset(float* out) noexcept {
    if (!out) return;
    uint32_t tmp[kStageRingSize];
    for (int i = 0; i < kStageCount; ++i) {
        const uint64_t sum = g_stageTimings.sumNs[i].exchange(0, std::memory_order_relaxed);
        const uint64_t n = g_stageTimings.count[i].exchange(0, std::memory_order_relaxed);
        const uint32_t mx = g_stageTimings.maxNs[i].exchange(0, std::memory_order_relaxed);
        const uint32_t written = g_stageTimings.ringIdx[i].exchange(0, std::memory_order_relaxed);
        const uint32_t m = (written > kStageRingSize) ? kStageRingSize : written;
        for (uint32_t k = 0; k < m; ++k)
            tmp[k] = g_stageTimings.ring[i][k].load(std::memory_order_relaxed);
        std::sort(tmp, tmp + m);
        const float mean = (n > 0) ? static_cast<float>(sum / n) : 0.0f;
        uint32_t p99 = 0;
        if (m > 0) {
            // Índice p99: ceil(0.99*m)-1 (m=100 → idx 98; m=1 → idx 0).
            const uint32_t idx = (m * 99 + 99) / 100 - 1;
            p99 = tmp[idx < m ? idx : m - 1];
        }
        out[i * 3 + 0] = mean;
        out[i * 3 + 1] = static_cast<float>(p99);
        out[i * 3 + 2] = static_cast<float>(mx);
    }
}

} // namespace ivanna
