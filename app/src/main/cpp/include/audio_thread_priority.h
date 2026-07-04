#pragma once

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define IVANNA_AUDIO_HAS_NEON 1
#else
#define IVANNA_AUDIO_HAS_NEON 0
#endif

#if defined(__ANDROID__)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace ivanna::audio {

inline void enableAudioThreadFastMath() noexcept {
#if IVANNA_AUDIO_HAS_NEON && defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24); // FZ (Flush-to-Zero)
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif IVANNA_AUDIO_HAS_NEON
    uint32_t fpscr;
    asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24); // FTZ
    fpscr |= (1u << 19); // DAZ
    asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
#endif
#if defined(__ANDROID__)
    static constexpr int kAndroidPriorityAudio = -16;
    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), kAndroidPriorityAudio);
#endif
}

inline void enableAudioThreadFastMathOnce() noexcept {
    thread_local bool initialized = false;
    if (initialized) return;
    initialized = true;
    enableAudioThreadFastMath();
}

} // namespace ivanna::audio
