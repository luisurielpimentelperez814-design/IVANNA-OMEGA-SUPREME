#pragma once

// pthread_setschedparam()/pthread_self() son POSIX y el NDK no garantiza
// que entren transitivamente desde <sched.h>. El build arm64 de CI fallaba
// con "use of undeclared identifier 'pthread_self'" al incluir este header
// desde los JNI de visualización. Incluir los contratos que usamos de forma
// explícita mantiene el header autocontenido y portable.
#include <cstdint>
#if defined(__ANDROID__)
#include <pthread.h>
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#define IVANNA_AUDIO_HAS_NEON 1
#else
#define IVANNA_AUDIO_HAS_NEON 0
#endif

#if defined(__ANDROID__)
#include <sys/resource.h>
#include <sched.h>
#include <sys/syscall.h>
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
    static constexpr int kAndroidPriorityAudio = -19;
    setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), kAndroidPriorityAudio);
    sched_param sp{};
    sp.sched_priority = 99;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
#endif
}

inline void enableAudioThreadFastMathOnce() noexcept {
    thread_local bool initialized = false;
    if (initialized) return;
    initialized = true;
    enableAudioThreadFastMath();
}

} // namespace ivanna::audio
