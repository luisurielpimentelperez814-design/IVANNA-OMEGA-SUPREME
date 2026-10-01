// test_rt_no_alloc.cpp — Fase A4 Detector dinámico en host de 0 malloc / 0 new en RT
//
// Intercepta operator new / operator new[] (y __sanitizer_malloc_hook bajo ASan/TSan)
// y verifica que con `ivanna::unified::inRealtimeAudioCallback() == true` ocurran
// exactamente CERO asignaciones de memoria dinámica durante 10 000 bloques por ruta
// (Ruta A, Ruta B, Ruta Híbrida) mientras los parámetros cambian continuamente.

#include <gtest/gtest.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

#include "include/omega_unified_dsp_stage.hpp"
#include "include/omega_wave_stages.hpp"
#include "supreme/SupremeAcousticStabilityGuard.hpp"
#include "spatial/HybridRenderer.hpp"
#include "spatial/RoomSimulator.hpp"
#include "spatial/IntelligentUpmixer.hpp"
#include "spatial/HoaBinauralDecoder.hpp"
#include "spatial/WfsRenderer.hpp"
#include "spatial/RirConvolver.hpp"

namespace {
std::atomic<size_t> g_rtAllocCount{0};
} // namespace

#if defined(IVANNA_SANITIZER_ACTIVE) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
extern "C" void __sanitizer_malloc_hook(const volatile void* /*ptr*/, size_t /*size*/) {
    if (ivanna::unified::inRealtimeAudioCallback()) {
        g_rtAllocCount.fetch_add(1, std::memory_order_relaxed);
    }
}
#else
void* operator new(std::size_t size) {
    if (ivanna::unified::inRealtimeAudioCallback()) {
        g_rtAllocCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (size == 0) size = 1;
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    if (ivanna::unified::inRealtimeAudioCallback()) {
        g_rtAllocCount.fetch_add(1, std::memory_order_relaxed);
    }
    if (size == 0) size = 1;
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void* ptr) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept {
    std::free(ptr);
}

void operator delete(void* ptr, std::size_t /*size*/) noexcept {
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t /*size*/) noexcept {
    std::free(ptr);
}
#endif

namespace {

constexpr size_t kBlockFrames = 64;
constexpr size_t kTenThousandBlocks = 10000;
constexpr float  kSampleRate = 48000.0f;

void fillTestSignal(float* L, float* R, size_t frames, size_t blockIdx) noexcept {
    const float basePhase = static_cast<float>(blockIdx * frames) * 0.045f;
    for (size_t i = 0; i < frames; ++i) {
        const float p = basePhase + static_cast<float>(i) * 0.045f;
        L[i] = 0.25f * std::sin(p);
        R[i] = 0.25f * std::cos(p * 1.03f);
    }
}

TEST(RtNoAllocDetectorTest, RouteA_UnifiedPipeline_10000BlocksZeroAlloc) {
    ivanna::unified::DeclarativeUnifiedPipeline pipeline;
    pipeline.prepare(kSampleRate, kBlockFrames);

    ivanna::supreme::SupremeAcousticStabilityGuard guard;
    guard.prepare(kSampleRate);

    auto& bus = ivanna::unified::UnifiedParamSnapshotBus::instance();
    bus.resetToAllOff();

    alignas(64) float bufL[kBlockFrames]{};
    alignas(64) float bufR[kBlockFrames]{};

    // Calentar 1 bloque fuera del detector por si alguna tabla estática se inicializa una vez
    fillTestSignal(bufL, bufR, kBlockFrames, 0);
    pipeline.process(bufL, bufR, kBlockFrames);
    guard.processBlock(bufL, bufR, kBlockFrames);

    g_rtAllocCount.store(0, std::memory_order_relaxed);

    for (size_t b = 0; b < kTenThousandBlocks; ++b) {
        // Cambiar parámetros desde fuera del callback antes del bloque
        if ((b & 63u) == 0u) {
            const bool en = ((b >> 6) & 1u) != 0u;
            const float intensity = 0.2f + 0.6f * static_cast<float>((b >> 6) % 5u) * 0.25f;
            bus.setStageEnabled(ivanna::unified::StageId::PhaseOracleControl, en, intensity);
            bus.setStageEnabled(ivanna::unified::StageId::CochlearPinn, en, intensity);
        }

        fillTestSignal(bufL, bufR, kBlockFrames, b);

        {
            ivanna::unified::RtCallbackSanitizerScope rtScope;
            pipeline.process(bufL, bufR, kBlockFrames);
            guard.processBlock(bufL, bufR, kBlockFrames);
        }
    }

    EXPECT_EQ(g_rtAllocCount.load(std::memory_order_relaxed), 0u)
        << "Ruta A realizó asignaciones de heap dentro de inRealtimeAudioCallback()";
}

TEST(RtNoAllocDetectorTest, RouteB_SpatialAndConvolver_10000BlocksZeroAlloc) {
    Ivanna::IntelligentUpmixer upmixer;
    upmixer.prepare(kSampleRate);
    upmixer.setUpmixingEnabled(true);

    Ivanna::HoaBinauralDecoder hoaDecoder;
    hoaDecoder.prepare(kSampleRate, 8);

    ivanna::spatial::WfsRenderer wfs;
    wfs.init(kSampleRate, static_cast<int>(kBlockFrames), 8);

    Ivanna::RirConvolver rir;
    rir.synthesizeMasterStudioBrir(0.34f, static_cast<int>(kSampleRate));

    std::vector<Ivanna::HoaVector> hoaField(kBlockFrames);
    alignas(64) float inL[kBlockFrames]{};
    alignas(64) float inR[kBlockFrames]{};
    alignas(64) float outL[kBlockFrames]{};
    alignas(64) float outR[kBlockFrames]{};

    // Warm-up de 1 bloque para asegurar que buffers estén dimensionados
    fillTestSignal(inL, inR, kBlockFrames, 0);
    upmixer.processBlock(inL, inR, hoaField, kBlockFrames);
    hoaDecoder.processBlock(hoaField, outL, outR, kBlockFrames);
    wfs.process(outL, outR, outL, outR, static_cast<int>(kBlockFrames));
    rir.process(outL, outR, static_cast<int>(kBlockFrames));

    g_rtAllocCount.store(0, std::memory_order_relaxed);

    for (size_t b = 0; b < kTenThousandBlocks; ++b) {
        if ((b & 31u) == 0u) {
            const float imm = 0.1f + 0.8f * static_cast<float>((b >> 5) % 4u) / 3.0f;
            upmixer.setImmersivity(imm);
            rir.setWetDry(0.15f + 0.2f * imm);
        }

        fillTestSignal(inL, inR, kBlockFrames, b);

        {
            ivanna::unified::RtCallbackSanitizerScope rtScope;
            upmixer.processBlock(inL, inR, hoaField, kBlockFrames);
            hoaDecoder.processBlock(hoaField, outL, outR, kBlockFrames);
            wfs.process(outL, outR, outL, outR, static_cast<int>(kBlockFrames));
            rir.process(outL, outR, static_cast<int>(kBlockFrames));
        }
    }

    EXPECT_EQ(g_rtAllocCount.load(std::memory_order_relaxed), 0u)
        << "Ruta B realizó asignaciones de heap dentro de inRealtimeAudioCallback()";
}

TEST(RtNoAllocDetectorTest, RouteHybrid_BinauralAndPlanar_10000BlocksZeroAlloc) {
    Ivanna::HybridRenderer hybrid;
    hybrid.setEnabled(true);

    alignas(64) float interleavedIn[kBlockFrames * 2]{};
    alignas(64) float interleavedOut[kBlockFrames * 2]{};
    alignas(64) float planarL[kBlockFrames]{};
    alignas(64) float planarR[kBlockFrames]{};

    g_rtAllocCount.store(0, std::memory_order_relaxed);

    for (size_t b = 0; b < kTenThousandBlocks; ++b) {
        if ((b & 15u) == 0u) {
            const float az = -60.0f + 120.0f * static_cast<float>((b >> 4) % 7u) / 6.0f;
            const float el = -15.0f + 30.0f * static_cast<float>((b >> 4) % 3u) / 2.0f;
            const float wet = 0.25f + 0.5f * static_cast<float>((b >> 4) % 4u) / 3.0f;
            hybrid.setVirtualAngles(az, el);
            hybrid.setBinauralWet(wet);
            hybrid.setRoomParameters(0.3f + 0.4f * wet, 0.35f, 0.40f, 0.25f, 0.35f);
        }

        fillTestSignal(planarL, planarR, kBlockFrames, b);
        for (size_t i = 0; i < kBlockFrames; ++i) {
            interleavedIn[2 * i]     = planarL[i];
            interleavedIn[2 * i + 1] = planarR[i];
        }

        {
            ivanna::unified::RtCallbackSanitizerScope rtScope;
            hybrid.renderBinaural(interleavedIn, interleavedOut, kBlockFrames);
            hybrid.renderPlanar(planarL, planarR, kBlockFrames);
        }
    }

    EXPECT_EQ(g_rtAllocCount.load(std::memory_order_relaxed), 0u)
        << "Ruta Híbrida realizó asignaciones de heap dentro de inRealtimeAudioCallback()";
}

} // namespace
