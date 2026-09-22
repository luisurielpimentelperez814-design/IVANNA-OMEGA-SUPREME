// ImeBridge.cpp — implementación del puente IME (cero malloc, seqlock).
#include "ImeBridge.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <time.h>

namespace ivanna { namespace ime {
namespace {
MusicFeatureExtractor g_ext;
MusicIntelligenceEngine g_eng;
ImeSharedState g_state;
bool g_prepared = false;
constexpr int kMaxFrames = 4096;
float g_tmpL[kMaxFrames];  // estáticos: prohibido malloc en feed
float g_tmpR[kMaxFrames];
inline uint64_t nowMs() noexcept {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}
} // namespace

ImeSharedState& imeShared() { return g_state; }

void imeFeedBlock(const float* interleaved, int frames) noexcept {
    if (!interleaved || frames <= 0) return;
    if (!g_state.enabled.load(std::memory_order_relaxed)) return;
    if (frames > kMaxFrames) frames = kMaxFrames;
    if (!g_prepared) { g_ext.prepare(48000.f, kMaxFrames); g_prepared = true; }
    for (int i = 0; i < frames; ++i) {
        g_tmpL[i] = interleaved[2 * i];
        g_tmpR[i] = interleaved[2 * i + 1];
    }
    g_ext.processBlock(g_tmpL, g_tmpR, frames);
    g_state.blocksFed.fetch_add(1, std::memory_order_relaxed);
    g_state.featSeq.fetch_add(1u, std::memory_order_acq_rel);   // impar: escribiendo
    g_state.latestFeatures = g_ext.features();
    g_state.featSeq.fetch_add(1u, std::memory_order_acq_rel);   // par: listo
}

int imeDecideNowJson(char* buf, int bufSize) noexcept {
    if (!buf || bufSize < 64) return 0;
    MusicFeatures f;
    int guard = 0;
    uint32_t s0, s1;
    do {
        s0 = g_state.featSeq.load(std::memory_order_acquire);
        f  = g_state.latestFeatures;
        s1 = g_state.featSeq.load(std::memory_order_acquire);
    } while ((s0 != s1 || (s0 & 1u) != 0u) && ++guard < 8);
    const uint64_t t0 = nowMs();
    const MusicDecision d = g_eng.decide(f);   // determinista, noexcept, sin malloc
    const uint64_t adaptMs = nowMs() - t0;
    g_state.decSeq.fetch_add(1u, std::memory_order_acq_rel);
    g_state.latestDecision = d;
    g_state.decSeq.fetch_add(1u, std::memory_order_acq_rel);
    return snprintf(buf, (size_t)bufSize,
        "{\"style\":\"%s\",\"confidence\":%.4f,\"wfsSpread\":%.4f,\"hrtfDepth\":%.4f,"
        "\"eqTiltDb\":%.4f,\"dynamicsAmount\":%.4f,\"envDepth\":%.4f,"
        "\"blocks\":%llu,\"adaptMs\":%llu}",
        d.style, (double)d.confidence, (double)d.wfsSpread, (double)d.hrtfDepth,
        (double)d.eqTiltDb, (double)d.dynamicsAmount, (double)d.envDepth,
        (unsigned long long)g_state.blocksFed.load(std::memory_order_relaxed),
        (unsigned long long)adaptMs);
}
}} // namespace ivanna::ime
