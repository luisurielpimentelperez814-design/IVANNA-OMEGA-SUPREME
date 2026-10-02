#include "ImeBridge.hpp"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace ivanna { namespace ime {

namespace {
ImeSharedState          g_sharedState{};
MusicFeatureExtractor   g_ext;
MusicIntelligenceEngine g_eng;
std::atomic<bool>       g_inited{false};
std::atomic<float>      g_lastSr{0.f};
StyleDecision           g_lastDec{};
std::atomic<int>        g_framesSinceTick{0};
std::atomic<int>        g_coldStartTicks{0};
std::atomic<int>        g_silentFrames{0};
std::atomic<bool>       g_wasSilent{true};

void ensureInit(float sr) noexcept {
    float prev = g_lastSr.load(std::memory_order_relaxed);
    if (!g_inited.load(std::memory_order_acquire) || prev != sr) {
        g_ext.prepare(sr > 8000.f ? sr : 48000.f, 8192);
        g_eng.prepare();
        g_lastSr.store(sr > 8000.f ? sr : 48000.f, std::memory_order_relaxed);
        g_framesSinceTick.store(0, std::memory_order_relaxed);
        g_coldStartTicks.store(0, std::memory_order_relaxed);
        g_silentFrames.store(0, std::memory_order_relaxed);
        g_wasSilent.store(true, std::memory_order_relaxed);
        g_inited.store(true, std::memory_order_release);
    }
}

void runDecisionTickInternal(float dtSec) noexcept {
    const auto f = g_ext.features();
    auto& bus = SceneTargetBus::instance();
    const int manualStyle = bus.manualStyleOverride();

    g_lastDec = g_eng.updateStateful(f, dtSec, manualStyle);

    SceneApply apply{};
    apply.t             = g_lastDec.t;
    apply.gate          = g_lastDec.gate;
    apply.conf          = g_lastDec.conf;
    apply.styleIndex    = g_lastDec.current;
    apply.presenceRatio = f.presenceRatio;
    apply.flatness1m    = f.flatness1m;
    apply.lateRatio     = bus.measuredLateRatio();

    // Si el usuario fijó un override manual de calidez (warmth >= 0), respetarlo
    const float userWarmth = bus.userWarmthOverride();
    if (userWarmth >= 0.0f) {
        apply.t.warmth = std::clamp(userWarmth, 0.0f, 1.0f);
        g_lastDec.t.warmth = apply.t.warmth;
        g_lastDec.blended.warmth = apply.t.warmth;
        g_lastDec.warmth = apply.t.warmth;
    }

    bus.publish(apply);
}

void onFramesFed(int frames, float sr) noexcept {
    g_sharedState.blocksFed.fetch_add(1, std::memory_order_relaxed);
    const float safeSr = (sr > 8000.f) ? sr : 48000.f;
    const auto f = g_ext.features();

    // Detección de silencio -> nueva pista (Sección 6: softReset)
    if (f.rms < 1.0e-4f) {
        const int sf = g_silentFrames.fetch_add(frames, std::memory_order_relaxed) + frames;
        if (sf >= static_cast<int>(safeSr * 0.8f)) {
            g_wasSilent.store(true, std::memory_order_relaxed);
        }
    } else {
        if (g_wasSilent.exchange(false, std::memory_order_relaxed)) {
            if (g_coldStartTicks.load(std::memory_order_relaxed) > 0) {
                g_eng.softReset();
            }
            g_coldStartTicks.store(0, std::memory_order_relaxed);
            g_framesSinceTick.store(0, std::memory_order_relaxed);
        }
        g_silentFrames.store(0, std::memory_order_relaxed);
    }

    const int accFrames = g_framesSinceTick.fetch_add(frames, std::memory_order_relaxed) + frames;
    const int coldTicks = g_coldStartTicks.load(std::memory_order_relaxed);
    // Arranque en frío: primeras 3 decisiones cada 0.5 s (identificación <= 1.5 s); luego cada 2.0 s
    const float windowSec = (coldTicks < 3) ? 0.5f : 2.0f;
    const int targetFrames = std::max(1, static_cast<int>(windowSec * safeSr));

    if (accFrames >= targetFrames) {
        g_framesSinceTick.store(0, std::memory_order_relaxed);
        if (coldTicks < 3 && f.rms >= 1.0e-4f) {
            g_coldStartTicks.fetch_add(1, std::memory_order_relaxed);
        }
        runDecisionTickInternal(windowSec);
    }
}
} // namespace

void* imeSharedOpaque() noexcept {
    return &g_sharedState;
}

void imeFeedBlock(const float* interleavedStereo, int frames, float sampleRate) noexcept {
    imeFeedInterleaved(interleavedStereo, frames, 2, sampleRate);
}

void imeFeedPlanar(const float* l, const float* r, int frames, float sampleRate) noexcept {
    if (!l || frames <= 0) return;
    ensureInit(sampleRate);
    g_ext.processBlock(l, r ? r : l, frames);
    onFramesFed(frames, sampleRate);
}

void imeFeedInterleaved(const float* interleaved, int frames, int channels, float sampleRate) noexcept {
    if (!interleaved || frames <= 0 || channels <= 0) return;
    ensureInit(sampleRate);
    constexpr int kChunk = 512;
    float l[kChunk], r[kChunk];
    int done = 0;
    while (done < frames) {
        int n = std::min(kChunk, frames - done);
        const float* p = interleaved + done * channels;
        if (channels == 1) {
            for (int i = 0; i < n; ++i) { l[i] = r[i] = p[i]; }
        } else {
            for (int i = 0; i < n; ++i) { l[i] = p[i * channels]; r[i] = p[i * channels + 1]; }
        }
        g_ext.processBlock(l, r, n);
        done += n;
    }
    onFramesFed(frames, sampleRate);
}

void imeSoftReset() noexcept {
    if (g_inited.load(std::memory_order_acquire)) {
        g_eng.softReset();
        g_coldStartTicks.store(0, std::memory_order_relaxed);
        g_framesSinceTick.store(0, std::memory_order_relaxed);
    }
}

int imeDecideNowJson(char* out, size_t maxLen) noexcept {
    if (!out || maxLen < 128) return 0;
    ensureInit(g_lastSr.load(std::memory_order_relaxed));
    const int coldTicks = g_coldStartTicks.load(std::memory_order_relaxed);
    const float dtSec = (coldTicks < 3) ? 0.5f : 1.0f;
    if (g_framesSinceTick.load(std::memory_order_relaxed) > 0 || g_lastDec.current < 0) {
        g_framesSinceTick.store(0, std::memory_order_relaxed);
        runDecisionTickInternal(dtSec);
    }
    const auto d = g_lastDec;
    const unsigned long long blocks = static_cast<unsigned long long>(
        g_sharedState.blocksFed.load(std::memory_order_relaxed));
    const int written = std::snprintf(out, maxLen,
        "{\"style\":\"%s\",\"index\":%d,\"blocks\":%llu,\"confidence\":%.3f,\"gate\":%.3f,"
        "\"wfsSpread\":%.3f,\"hrtfDepth\":%.3f,\"eqTiltDb\":%.2f,"
        "\"dynamicsAmount\":%.3f,\"envDepth\":%.3f,\"warmth\":%.3f}",
        d.profileName ? d.profileName : "neutral",
        d.profileIndex,
        blocks,
        d.confidence,
        d.gate,
        d.t.wfsSpread,
        d.t.hrtfDepth,
        d.t.eqTiltDb,
        d.t.dynamicsAmount,
        d.t.envDepth,
        d.t.warmth);
    if (written <= 0 || static_cast<size_t>(written) >= maxLen) {
        out[0] = '\0';
        return 0;
    }
    return written;
}

std::string imeDecideNowJson() {
    ensureInit(g_lastSr.load(std::memory_order_relaxed));
    const int coldTicks = g_coldStartTicks.load(std::memory_order_relaxed);
    const float dtSec = (coldTicks < 3) ? 0.5f : 1.0f;
    if (g_framesSinceTick.load(std::memory_order_relaxed) > 0 || g_lastDec.current < 0) {
        g_framesSinceTick.store(0, std::memory_order_relaxed);
        runDecisionTickInternal(dtSec);
    }

    const auto f = g_ext.features();
    const auto d = g_lastDec;
    const auto& bus = SceneTargetBus::instance();
    const auto m10 = bus.readRealismMetrics();
    const float* probs = g_eng.blender().probabilities();
    const unsigned long long blocks = static_cast<unsigned long long>(
        g_sharedState.blocksFed.load(std::memory_order_relaxed));

    char buf[1600];
    std::snprintf(buf, sizeof(buf),
        "{\"style\":\"%s\",\"index\":%d,\"blocks\":%llu,\"confidence\":%.3f,\"gate\":%.3f,"
        "\"wfsSpread\":%.3f,\"hrtfDepth\":%.3f,\"eqTiltDb\":%.2f,"
        "\"dynamicsAmount\":%.3f,\"envDepth\":%.3f,\"warmth\":%.3f,"
        "\"subPunch\":%.3f,\"vocalIntimacy\":%.3f,\"airHolography\":%.3f,"
        "\"stageElevation\":%.3f,\"targetIacc\":%.3f,"
        "\"rms\":%.4f,\"crestDb\":%.2f,\"bassRatio\":%.3f,"
        "\"midRatio\":%.3f,\"trebleRatio\":%.3f,\"stereoWidth\":%.3f,"
        "\"transientRate\":%.3f,\"density\":%.3f,"
        "\"presenceRatio\":%.3f,\"airRatio\":%.3f,\"flatness1m\":%.3f,"
        "\"onsetRegularity\":%.3f,\"lraProxy12\":%.3f,\"sideMid\":%.3f,"
        "\"subBandRatio\":%.3f,\"bodyBandRatio\":%.3f,\"defBandRatio\":%.3f,"
        "\"lateRatio\":%.3f,"
        "\"cTransient\":%.3f,\"cSpatial\":%.3f,\"cDynamic\":%.3f,\"qScore\":%.3f,"
        "\"iaccOut\":%.3f,\"guardScale\":%.3f,\"guardActive\":%s,"
        "\"sceneEnabled\":%s,\"useStatDereverb\":%s,\"usePhysicalEr\":%s,\"shaperMode\":%d,"
        "\"manualStyle\":%d,"
        "\"probs\":[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]}",
        d.profileName ? d.profileName : "neutral",
        d.profileIndex,
        blocks,
        d.confidence,
        d.gate,
        d.t.wfsSpread,
        d.t.hrtfDepth,
        d.t.eqTiltDb,
        d.t.dynamicsAmount,
        d.t.envDepth,
        d.t.warmth,
        d.t.subPunch,
        d.t.vocalIntimacy,
        d.t.airHolography,
        d.t.stageElevation,
        d.t.targetIacc,
        f.rms, f.crestDb, f.bassRatio,
        f.midRatio, f.trebleRatio, f.stereoWidth,
        f.transientRate, f.density,
        f.presenceRatio, f.airRatio, f.flatness1m,
        f.onsetRegularity, f.lraProxy12, f.sideMid,
        f.subBandRatio, f.bodyBandRatio, f.defBandRatio,
        bus.measuredLateRatio(),
        m10.cTransient, m10.cSpatial, m10.cDynamic, m10.qScore,
        m10.iaccOut, m10.guardScale, m10.guardActive ? "true" : "false",
        bus.isSceneReconstructionEnabled() ? "true" : "false",
        bus.useStatDereverb() ? "true" : "false",
        bus.usePhysicalEr() ? "true" : "false",
        bus.shaperMode(),
        bus.manualStyleOverride(),
        probs[0], probs[1], probs[2], probs[3], probs[4], probs[5],
        probs[6], probs[7], probs[8], probs[9], probs[10], probs[11]);
    return std::string(buf);
}

StyleDecision imeLastDecision() noexcept { return g_lastDec; }
MusicFeatures imeLastFeatures() noexcept { return g_ext.features(); }

}} // namespace ivanna::ime
