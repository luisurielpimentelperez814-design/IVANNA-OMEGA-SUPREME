// ImeBridge.hpp — puente global libre de locks entre las rutas de audio
// (Oboe / AudioTrack / omega_effect) y el motor MusicIntelligenceEngine + SceneTargetBus.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
#include "MusicIntelligenceEngine.hpp"
#include "SceneTargetBus.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ivanna { namespace ime {

struct ImeSharedState {
    std::atomic<bool>     enabled{true};
    std::atomic<uint64_t> blocksFed{0};
};

// Acceso opaco al estado compartido atómico (compatibilidad con test_ime_bridge)
void* imeSharedOpaque() noexcept;

// Alimenta un bloque intercalado estéreo (L,R,L,R,...) de 2 canales.
void imeFeedBlock(const float* interleavedStereo, int frames, float sampleRate = 48000.0f) noexcept;

// Alimenta un bloque intercalado (L,R,L,R,...) al extractor global.
// RT-safe: sin locks, sin malloc. Ejecuta además el tick amortizado de arranque en frío
// (cada 0.5 s las primeras 3 ventanas, luego cada 2.0 s) publicando en SceneTargetBus.
void imeFeedInterleaved(const float* interleaved, int frames, int channels, float sampleRate) noexcept;

// Alimenta un bloque planar L/R (como en IvannaFusionEngine::processBlock / omega_effect).
void imeFeedPlanar(const float* l, const float* r, int frames, float sampleRate) noexcept;

// Ejecuta la decisión sobre las features acumuladas, publica en SceneTargetBus y devuelve JSON.
// Llamar desde un hilo de baja prioridad (worker / UI) cada 0.5–2 s.
std::string imeDecideNowJson();

// Sobrecarga sin heap en buffer fijo (compatibilidad con test_ime_bridge).
// Devuelve bytes escritos (>0) o 0 si maxLen es insuficiente.
int imeDecideNowJson(char* out, size_t maxLen) noexcept;

// Acceso directo a la última decisión / features para pruebas y telemetría nativa.
StyleDecision imeLastDecision() noexcept;
MusicFeatures imeLastFeatures() noexcept;

// Reinicio suave ante cambio manual de pista o ruta
void imeSoftReset() noexcept;

}} // namespace ivanna::ime
