// ImeBridge.hpp — puente RT-safe entre el hilo de captura y el worker de decisión.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS.
// Patrón: seqlock (impar=escribiendo) + atomics relaxed. Sin locks, sin malloc,
// sin STL en el camino de feed. El JSON se construye solo en imeDecideNowJson
// (hilo worker, cada ~2 s — nunca en el callback de audio).
#pragma once
#include <atomic>
#include <cstdint>
#include "MusicIntelligenceEngine.hpp"

namespace ivanna { namespace ime {

struct ImeSharedState {
    std::atomic<bool>     enabled{false};   // el usuario lo activa en el panel
    std::atomic<uint64_t> blocksFed{0};
    std::atomic<uint32_t> featSeq{0};       // seqlock de features
    MusicFeatures         latestFeatures{};
    std::atomic<uint32_t> decSeq{0};        // seqlock de decisión
    MusicDecision         latestDecision{};
};

// FIX (build 87cd652d): la declaración NO debe heredar extern "C" del
// archivo .cpp — devuelve un tipo C++ (ImeSharedState&) y el NDK la
// rechaza con -Werror=return-type-c-linkage. Al ser una definición en C++
// puro (namespace ivanna::ime), la declaración limpia en C++ es correcta.
ImeSharedState& imeShared();

// RT-safe: interleaved estéreo [L0,R0,...], frames = muestras por canal.
// Deinterleava a buffers estáticos preasignados y acumula en el extractor.
void imeFeedBlock(const float* interleaved, int frames) noexcept;

// Hilo worker (NO RT): decide sobre las últimas features y devuelve JSON.
// Devuelve bytes escritos (0 si buf inválido).
int imeDecideNowJson(char* buf, int bufSize) noexcept;

}} // namespace ivanna::ime
