#pragma once
// npe_analysis_tap.hpp — análisis SOLO-LECTURA del NPE (género + firma del Synthesizer).
//
// Por qué existe: con DSPBridge nativo activo el servicio de captura NO ejecuta el NPE sobre el
// audio (árbitro de ruta única: evita triple saturación y doble HRTF/peine). Pero el género y la
// firma del Synthesizer solo se actualizaban dentro de Engine::processStereo, así que en ese caso
// la UI mostraba "—" / "0 % confidence" para siempre aunque el NPE estuviera "listo".
//
// Este tap alimenta al MISMO AutonomousBrain/Synthesizer con la entrada, sin escribir audio ni
// tocar el estado de procesado (LIF/NHO/AGC). Coste: 3 biquads + acumuladores por muestra y una
// evaluación cada kWindowSize muestras (sin FFT, sin heap): cabe en el hilo de audio.
//
// RT-safe: sin asignaciones ni bloqueos; O(n).
#include <algorithm>

#include "autonomous_brain.hpp"
#include "ivanna_synthesizer.hpp"

namespace ivanna::acoustic {

inline void npeAnalyzeStereo(AutonomousBrain& brain, Synthesizer& synth,
                             const float* inL, const float* inR, int n,
                             float sampleRate) noexcept {
    if (!inL || !inR || n <= 0) return;
    constexpr int kChunk = 512;
    float mono[kChunk];
    for (int off = 0; off < n; off += kChunk) {
        const int m = std::min(n - off, kChunk);
        for (int i = 0; i < m; ++i) mono[i] = 0.5f * (inL[off + i] + inR[off + i]);
        brain.processBlock(mono, m, synth);
    }
    synth.smoothTick(n, sampleRate);
}

} // namespace ivanna::acoustic
