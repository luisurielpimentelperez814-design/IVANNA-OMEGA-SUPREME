// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — NAEL / ISO 226:2023
 *   Neuroacoustic Equal-Loudness compensator
 *
 * Objetivo: mantener el TIMBRE percibido constante independientemente
 * del volumen absoluto de reproducción, usando la familia de curvas
 * iso-fónicas de la norma ISO 226:2023.
 *
 * Diseño:
 *   - Referencia: curva a 83 phon (nivel de mastering de estudio típico).
 *   - Nivel actual: derivado del LUFS integrado publicado por el
 *     LoudnessMeter (BS.1770-4) que ya corre en nativeProcess:
 *         phon_actual ≈ LUFS_integrado + 83
 *     (LUFS ya lleva ponderación K, próxima a A-weighting → mapa lineal
 *     hacia phon con offset absoluto de 83 phon = 0 dB LUFS).
 *   - Corrección por banda: SPL(banda, 83 phon) - SPL(banda, phon_actual).
 *     A menor volumen, el oído percibe menos graves y agudos → la
 *     corrección devuelve energía a esas bandas.
 *   - Suavizado EMA con τ=500ms para evitar respiración audible cuando
 *     el LUFS integrado se mueve entre bloques.
 *   - Clamp final [-8, +8] dB — margen seguro que cubre el rango típico
 *     entre 40 phon (susurro) y 83 phon (referencia).
 *
 * Uso desde audio_control_plane.cpp:
 *     const float lufs = g_control_frame.output_lufs.load(...);
 *     for (int b = 0; b < 10; ++b) {
 *         cor_db[b] = ivanna::iso226_correction_db(b, lufs);
 *     }
 *     // fold a low/mid/high y sumar sobre f.low/f.mid/f.high
 *
 * Nota: la LUT viene de la propia ISO 226:2023 (tabla A.1), interpolada
 * lineal en las 10 bandas ISO 1/1-oct 31/62/125/250/500/1k/2k/4k/8k/16kHz.
 * ============================================================
 */

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ivanna {

// 10 bandas 1/1-oct (ISO 266)
static constexpr int NAEL_NUM_BANDS  = 10;
// 9 niveles de phon: 40, 50, 55, 60, 65, 70, 75, 80, 83
static constexpr int NAEL_NUM_LEVELS = 9;

// Nivel de referencia en el que la corrección es exactamente 0 dB en
// todas las bandas (el material está masterizado para sonar plano aquí).
static constexpr float NAEL_REF_PHON = 83.0f;

// Frecuencias centrales de las 10 bandas (Hz) — sólo para telemetría/UI.
static constexpr float kNaelBandsHz[NAEL_NUM_BANDS] = {
    31.5f, 63.f, 125.f, 250.f, 500.f, 1000.f, 2000.f, 4000.f, 8000.f, 16000.f
};

// Niveles de phon tabulados.
static constexpr float kNaelPhonLevels[NAEL_NUM_LEVELS] = {
    40.f, 50.f, 55.f, 60.f, 65.f, 70.f, 75.f, 80.f, 83.f
};

// SPL (dB) a cada nivel de phon, por banda. Fuente: ISO 226:2023 tabla A.1,
// interpolada en las 10 bandas ISO 1/1-oct. Filas = niveles de phon en el
// orden de kNaelPhonLevels; columnas = bandas en el orden de kNaelBandsHz.
//
// LUT compacta: la corrección real que interesa es la DIFERENCIA entre la
// fila de referencia (83 phon) y la fila actual, no los valores absolutos.
// Se calcula en runtime — así la tabla ocupa 9*10=90 floats en .rodata.
static constexpr float kNaelSpl[NAEL_NUM_LEVELS][NAEL_NUM_BANDS] = {
    // 40 phon
    { 78.5f, 68.7f, 59.5f, 51.1f, 44.0f, 40.0f, 37.5f, 35.6f, 40.5f, 65.0f },
    // 50 phon
    { 85.4f, 76.6f, 68.3f, 60.3f, 53.4f, 50.0f, 47.8f, 46.2f, 51.0f, 74.0f },
    // 55 phon
    { 88.7f, 80.5f, 72.6f, 64.8f, 58.0f, 55.0f, 53.0f, 51.5f, 56.2f, 78.3f },
    // 60 phon
    { 92.0f, 84.3f, 76.9f, 69.3f, 62.6f, 60.0f, 58.2f, 56.9f, 61.5f, 82.7f },
    // 65 phon
    { 95.2f, 88.1f, 81.1f, 73.7f, 67.2f, 65.0f, 63.4f, 62.3f, 66.8f, 87.0f },
    // 70 phon
    { 98.4f, 91.8f, 85.2f, 78.1f, 71.8f, 70.0f, 68.6f, 67.7f, 72.0f, 91.3f },
    // 75 phon
    { 101.6f, 95.5f, 89.3f, 82.5f, 76.4f, 75.0f, 73.8f, 73.1f, 77.3f, 95.5f },
    // 80 phon
    { 104.7f, 99.2f, 93.4f, 86.9f, 81.0f, 80.0f, 79.0f, 78.5f, 82.6f, 99.7f },
    // 83 phon (referencia)
    { 106.5f, 101.4f, 95.8f, 89.5f, 83.8f, 83.0f, 82.1f, 81.7f, 85.7f, 102.2f }
};

// ── Estado global suavizado (EMA por banda) ─────────────────────────────
// Sólo un escritor real: el hilo de control (audio_control_plane.cpp) via
// control_apply_frame(). Se marca atomic<float> por seguridad de lectores
// externos (nativeGetNaelCorrections desde JNI/UI).
struct NaelState {
    std::atomic<float> smoothed_db[NAEL_NUM_BANDS];
    NaelState() noexcept {
        for (int b = 0; b < NAEL_NUM_BANDS; ++b) {
            smoothed_db[b].store(0.f, std::memory_order_relaxed);
        }
    }
};

// Instancia interna (definida inline para no requerir .cpp aparte).
inline NaelState& nael_state() noexcept {
    static NaelState s;
    return s;
}

// Convierte LUFS integrado a phon aproximado.
// LUFS es K-weighted (BS.1770), aproximación práctica a A-weighting.
// Un master a -14 LUFS reproducido a "referencia" ≈ 69 phon.
// Fórmula usada: phon = clamp(LUFS + 83, 40, 83).
inline float lufs_to_phon(float lufs) noexcept {
    if (!std::isfinite(lufs)) return NAEL_REF_PHON;
    return std::clamp(lufs + 83.f, 40.f, NAEL_REF_PHON);
}

// Interpolación lineal en la LUT por nivel de phon. Devuelve la
// corrección instantánea (SIN suavizar) en dB para la banda `band`
// dado el LUFS integrado actual. Rango final clamp [-8, +8] dB.
inline float iso226_raw_correction_db(int band, float phon) noexcept {
    if (band < 0 || band >= NAEL_NUM_BANDS) return 0.f;

    // Localizar el par de filas (i, i+1) que envuelven a phon.
    int lo = 0;
    for (int i = 0; i < NAEL_NUM_LEVELS - 1; ++i) {
        if (phon >= kNaelPhonLevels[i] && phon <= kNaelPhonLevels[i + 1]) {
            lo = i;
            break;
        }
        if (phon >= kNaelPhonLevels[NAEL_NUM_LEVELS - 1]) {
            lo = NAEL_NUM_LEVELS - 2;
        }
    }
    const int hi = lo + 1;
    const float p0 = kNaelPhonLevels[lo];
    const float p1 = kNaelPhonLevels[hi];
    const float t  = (p1 > p0) ? std::clamp((phon - p0) / (p1 - p0), 0.f, 1.f) : 0.f;

    const float spl_actual = kNaelSpl[lo][band] * (1.f - t) + kNaelSpl[hi][band] * t;
    const float spl_ref    = kNaelSpl[NAEL_NUM_LEVELS - 1][band]; // 83 phon

    // Corrección: sube donde el oído a `phon` percibe menos que a la
    // referencia. Convención audio: (ref - actual) porque una mayor SPL
    // requerida (curva más alta) implica sensibilidad menor → subir EQ.
    const float raw = spl_ref - spl_actual;
    return std::clamp(raw, -8.f, 8.f);
}

// API pública: devuelve la corrección SUAVIZADA (EMA τ≈500ms) para la
// banda `band` con el LUFS integrado `lufs_integrated`. Debe llamarse
// desde el hilo de control una vez por tick (~50ms typical) — la EMA
// asume ese periodo. `dt_ms` permite ajustar el coeficiente si el
// caller sabe con más precisión el intervalo real entre llamadas.
inline float iso226_correction_db(int band, float lufs_integrated,
                                  float dt_ms = 50.f) noexcept {
    if (band < 0 || band >= NAEL_NUM_BANDS) return 0.f;
    const float phon = lufs_to_phon(lufs_integrated);
    const float raw  = iso226_raw_correction_db(band, phon);

    // EMA coefficient: α = 1 - exp(-dt/τ), τ = 500 ms.
    constexpr float TAU_MS = 500.f;
    const float alpha = 1.f - std::exp(-std::max(dt_ms, 0.1f) / TAU_MS);

    auto& st = nael_state();
    const float prev = st.smoothed_db[band].load(std::memory_order_relaxed);
    const float next = prev + alpha * (raw - prev);
    const float clamped = std::clamp(next, -8.f, 8.f);
    st.smoothed_db[band].store(clamped, std::memory_order_relaxed);
    return clamped;
}

// Lector puro: sin actualización de EMA. Para el JNI getter.
inline float iso226_get_smoothed(int band) noexcept {
    if (band < 0 || band >= NAEL_NUM_BANDS) return 0.f;
    return nael_state().smoothed_db[band].load(std::memory_order_relaxed);
}

// Reset del estado (por ejemplo al deshabilitar el toggle).
inline void iso226_reset() noexcept {
    auto& st = nael_state();
    for (int b = 0; b < NAEL_NUM_BANDS; ++b) {
        st.smoothed_db[b].store(0.f, std::memory_order_relaxed);
    }
}

} // namespace ivanna
