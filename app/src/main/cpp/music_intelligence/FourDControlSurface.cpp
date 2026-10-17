// FourDControlSurface.cpp — implementación de la superficie global de la Cadena 4D
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#include "FourDControlSurface.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>

namespace ivanna { namespace fourd {

namespace {

// Chunk fijo: el mismo tamaño que usa el motor IME (ImeBridge kChunk=512) y holgadamente
// por debajo del maxBlock de arranque (480). Búferes en pila -> cero malloc en audio.
constexpr int kChunk = 480;

std::mutex   g_initMutex;
std::unique_ptr<ivanna::ime::FourDExcitationChain> g_chain;
std::atomic<bool>  g_ready{false};
std::atomic<bool>  g_enabled{false};
std::atomic<bool>  g_hapticsOn{false};
std::atomic<bool>  g_xtcOn{false};
std::atomic<float> g_sr{48000.0f};
std::atomic<int>   g_maxBlock{480};

inline bool active() noexcept {
    return g_ready.load(std::memory_order_acquire) && g_chain != nullptr;
}

inline ivanna::ime::FourDExcitationChain* chain() noexcept { return g_chain.get(); }

void reapplyControls() noexcept {
    auto* c = chain();
    if (!c) return;
    c->setEnabled(g_enabled.load(std::memory_order_relaxed));
    c->setHapticsEnabled(g_hapticsOn.load(std::memory_order_relaxed));
    c->setCrosstalkEnabled(g_xtcOn.load(std::memory_order_relaxed));
}

} // namespace

// ── Ciclo de vida ────────────────────────────────────────────────────────────
void ensureInit(float sampleRate, int maxBlock) noexcept {
    const float sr = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
    const int   mb = (maxBlock > 0) ? maxBlock : 480;
    std::lock_guard<std::mutex> g(g_initMutex);
    if (g_ready.load(std::memory_order_acquire) && g_chain) {
        // Re-preparar solo si cambian las condiciones (cambio de ruta / sample rate).
        if (g_sr.load(std::memory_order_relaxed) == sr &&
            g_maxBlock.load(std::memory_order_relaxed) == mb) {
            return;
        }
    }
    if (!g_chain) g_chain = std::make_unique<ivanna::ime::FourDExcitationChain>();
    g_chain->prepare(sr, mb);
    g_sr.store(sr, std::memory_order_relaxed);
    g_maxBlock.store(mb, std::memory_order_relaxed);
    g_ready.store(true, std::memory_order_release);
    reapplyControls();
}

bool isInitialized() noexcept { return active(); }

void shutdown() noexcept {
    std::lock_guard<std::mutex> g(g_initMutex);
    g_ready.store(false, std::memory_order_release);
    g_chain.reset();
}

// ── Interruptor maestro y sub-controles ──────────────────────────────────────
void setEnabled(bool on) noexcept {
    g_enabled.store(on, std::memory_order_relaxed);
    if (active()) chain()->setEnabled(on);
}
bool enabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

void setHapticsEnabled(bool on) noexcept {
    g_hapticsOn.store(on, std::memory_order_relaxed);
    if (active()) chain()->setHapticsEnabled(on);
}
void setHapticScale(float s) noexcept              { if (active()) chain()->setHapticScale(s); }
void setOutputLatencyMs(float ms) noexcept         { if (active()) chain()->setOutputLatencyMs(ms); }
void setLeadMs(float ms) noexcept                  { if (active()) chain()->setLeadMs(ms); }
void setDepth(float d) noexcept                    { if (active()) chain()->setDepth(d); }
void setDosimeterMode(int mode) noexcept           { if (active()) chain()->setDosimeterMode(mode); }
void setDosimeterCalibration(float dbfs) noexcept  { if (active()) chain()->setDosimeterCalibration(dbfs); }
void setCrosstalkEnabled(bool on) noexcept {
    g_xtcOn.store(on, std::memory_order_relaxed);
    if (active()) chain()->setCrosstalkEnabled(on);
}
void setSpeakerGeometry(const ivanna::spatial::CrosstalkCanceller::Geometry& g) noexcept {
    if (active()) chain()->setSpeakerGeometry(g);
}

// ── Alimentación ─────────────────────────────────────────────────────────────
void feedPlanar(const float* L, const float* R, int frames) noexcept {
    if (!L || frames <= 0) return;
    if (!active() || !g_enabled.load(std::memory_order_relaxed)) return;
    auto* c = chain();
    const float* rr = R ? R : L;
    int done = 0;
    while (done < frames) {
        const int n = std::min(kChunk, frames - done);
        c->analyze(L + done, rr + done, n);
        done += n;
    }
}

void feedInterleaved(const float* interleaved, int frames, int channels) noexcept {
    if (!interleaved || frames <= 0 || channels <= 0) return;
    if (!active() || !g_enabled.load(std::memory_order_relaxed)) return;
    auto* c = chain();
    float l[kChunk], r[kChunk];
    int done = 0;
    while (done < frames) {
        const int n = std::min(kChunk, frames - done);
        const float* p = interleaved + static_cast<size_t>(done) * channels;
        if (channels == 1) {
            for (int i = 0; i < n; ++i) { l[i] = r[i] = p[i]; }
        } else {
            for (int i = 0; i < n; ++i) { l[i] = p[i * channels]; r[i] = p[i * channels + 1]; }
        }
        c->analyze(l, r, n);
        done += n;
    }
}

// ── Etapa de salida ──────────────────────────────────────────────────────────
void applyOutputPlanar(float* L, float* R, int frames) noexcept {
    if (!L || !R || frames <= 0) return;
    if (!active() || !g_enabled.load(std::memory_order_relaxed)) return;
    auto* c = chain();
    int done = 0;
    while (done < frames) {
        const int n = std::min(kChunk, frames - done);
        c->applyOutputStage(L + done, R + done, n);
        done += n;
    }
}

void applyOutputInterleaved(float* interleaved, int frames, int channels) noexcept {
    if (!interleaved || frames <= 0 || channels <= 0) return;
    if (!active() || !g_enabled.load(std::memory_order_relaxed)) return;
    auto* c = chain();
    float l[kChunk], r[kChunk];
    int done = 0;
    while (done < frames) {
        const int n = std::min(kChunk, frames - done);
        float* p = interleaved + static_cast<size_t>(done) * channels;
        if (channels == 1) {
            for (int i = 0; i < n; ++i) { l[i] = r[i] = p[i]; }
            c->applyOutputStage(l, r, n);
            for (int i = 0; i < n; ++i) { p[i] = l[i]; }
        } else {
            for (int i = 0; i < n; ++i) { l[i] = p[i * channels]; r[i] = p[i * channels + 1]; }
            c->applyOutputStage(l, r, n);
            for (int i = 0; i < n; ++i) { p[i * channels] = l[i]; p[i * channels + 1] = r[i]; }
        }
        done += n;
    }
}

// ── Lazo de escena ───────────────────────────────────────────────────────────
void publishAnticipatedScene() noexcept {
    if (!active() || !g_enabled.load(std::memory_order_relaxed)) return;
    auto& bus = ivanna::ime::SceneTargetBus::instance();
    const ivanna::ime::SceneApply base = bus.peekLatest();
    chain()->publishScene(base);
}

// ── Estado ───────────────────────────────────────────────────────────────────
ivanna::ime::FourDStatus status() noexcept {
    if (!active()) return ivanna::ime::FourDStatus{};
    return chain()->status();
}

uint32_t publishedScenes() noexcept {
    if (!active()) return 0u;
    return chain()->status().publishedScenes;
}

}} // namespace ivanna::fourd
