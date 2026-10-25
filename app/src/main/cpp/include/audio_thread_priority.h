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
    #if defined(__aarch64__)
    uint64_t fpcr;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);
    fpcr |= (1ULL << 0);
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif defined(__arm__) || defined(__ARM_ARCH_7A__)
    uint32_t fpscr;
    asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24);
    fpscr |= (1u << 19);
    asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
#endif
#elif IVANNA_AUDIO_HAS_NEON
    uint32_t fpscr;
    asm volatile("vmrs %0, fpscr" : "=r"(fpscr));
    fpscr |= (1u << 24); // FTZ
    fpscr |= (1u << 19); // DAZ
    asm volatile("vmsr fpscr, %0" : : "r"(fpscr));
#endif
#endif
}

// Scheduling is deliberately separate from FP-mode setup. The generic helper
// above is also called from JNI, visualizer initialization and constructors;
// promoting those callers to FIFO/99 can starve touch and system services.
// Call this only from a dedicated, measured audio-deadline thread.
inline bool requestCurrentThreadRealtimePriority(int priority) noexcept {
#if defined(__ANDROID__)
    if (priority < 1 || priority > 99) return false;
    sched_param sp{};
    sp.sched_priority = priority;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) return false;
    int actual_policy = 0;
    sched_param actual{};
    return pthread_getschedparam(pthread_self(), &actual_policy, &actual) == 0 &&
           actual_policy == SCHED_FIFO && actual.sched_priority == priority;
#else
    (void)priority;
    return false;
#endif
}

inline void enableAudioThreadFastMathOnce() noexcept {
    thread_local bool initialized = false;
    if (initialized) return;
    initialized = true;
    enableAudioThreadFastMath();
}

} // namespace ivanna::audio
