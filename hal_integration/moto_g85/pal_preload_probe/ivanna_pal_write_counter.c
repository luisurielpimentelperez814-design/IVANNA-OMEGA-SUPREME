#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "IVANNA_PAL_WRITE", __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#endif

/*
 * Observation-only LD_PRELOAD probe for pal_stream_write().
 *
 * It forwards every call UNCHANGED (same stream handle, same buffer pointer,
 * same return value) and only counts: calls, bytes, min/max/last write size,
 * short/failed returns and the largest gap between calls, per stream handle.
 * It never reads PCM samples, never writes to the buffer, never takes a lock,
 * allocates or does I/O on the audio thread. A separate detached thread dumps
 * a snapshot every few seconds. Layout assumption (verified in the HAL
 * disassembly of StreamOutPrimary::write): struct pal_buffer { uint8_t
 * *buffer; size_t size; ... } — only `size` is read.
 */
#ifndef IVANNA_OUT_PATH
#define IVANNA_OUT_PATH "/data/vendor/ivanna_audio/pal_write_counter.txt"
#endif
#ifndef IVANNA_OFF_PATH
#define IVANNA_OFF_PATH "/data/vendor/ivanna_audio/pal_counter.off"
#endif
#ifndef IVANNA_DUMP_PERIOD_S
#define IVANNA_DUMP_PERIOD_S 5
#endif
#define SLOTS 16
#define MAX_SANE_SIZE (16u * 1024u * 1024u)

struct pal_buffer_head { void *buffer; size_t size; };

struct slot {
    _Atomic(uintptr_t) handle;
    atomic_ullong calls, bytes, short_ret, err_ret, first_ns, last_ns, max_gap_ns;
    atomic_ullong min_size, max_size, last_size;
};
static struct slot g_slots[SLOTS];
static atomic_ullong g_total_calls, g_table_full, g_bad_buf;
static atomic_int g_enabled = 1;
static _Atomic(ssize_t (*)(void *, void *)) g_real;

static unsigned long long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec;
}
static void amax(atomic_ullong *a, unsigned long long v) {
    unsigned long long c = atomic_load_explicit(a, memory_order_relaxed);
    while (v > c && !atomic_compare_exchange_weak_explicit(a, &c, v, memory_order_relaxed, memory_order_relaxed)) {}
}
static void amin(atomic_ullong *a, unsigned long long v) {
    unsigned long long c = atomic_load_explicit(a, memory_order_relaxed);
    while ((c == 0 || v < c) && !atomic_compare_exchange_weak_explicit(a, &c, v, memory_order_relaxed, memory_order_relaxed)) {}
}
static struct slot *find_slot(uintptr_t h) {
    unsigned i = (unsigned)((h >> 4) * 2654435761u) % SLOTS;
    for (unsigned n = 0; n < SLOTS; ++n, i = (i + 1) % SLOTS) {
        uintptr_t cur = atomic_load_explicit(&g_slots[i].handle, memory_order_acquire);
        if (cur == h) return &g_slots[i];
        if (cur == 0) {
            uintptr_t exp = 0;
            if (atomic_compare_exchange_strong(&g_slots[i].handle, &exp, h) || exp == h) return &g_slots[i];
        }
    }
    return NULL;
}

__attribute__((visibility("default"))) ssize_t pal_stream_write(void *stream, void *buf) {
    ssize_t (*real)(void *, void *) = atomic_load_explicit(&g_real, memory_order_acquire);
    if (!real) {
        real = (ssize_t (*)(void *, void *))dlsym(RTLD_NEXT, "pal_stream_write");
        if (!real) { errno = ENOSYS; return -ENOSYS; }
        atomic_store_explicit(&g_real, real, memory_order_release);
    }
    size_t size = 0;
    int valid = 0;
    if (atomic_load_explicit(&g_enabled, memory_order_relaxed) && buf && ((uintptr_t)buf & 7u) == 0) {
        size = ((struct pal_buffer_head *)buf)->size;
        valid = size > 0 && size <= MAX_SANE_SIZE;
        if (!valid) atomic_fetch_add_explicit(&g_bad_buf, 1, memory_order_relaxed);
    }
    unsigned long long t = valid ? now_ns() : 0;
    ssize_t r = real(stream, buf);
    if (valid) {
        struct slot *s = find_slot((uintptr_t)stream);
        atomic_fetch_add_explicit(&g_total_calls, 1, memory_order_relaxed);
        if (!s) atomic_fetch_add_explicit(&g_table_full, 1, memory_order_relaxed);
        else {
            atomic_fetch_add_explicit(&s->calls, 1, memory_order_relaxed);
            if (r < 0) atomic_fetch_add_explicit(&s->err_ret, 1, memory_order_relaxed);
            else {
                atomic_fetch_add_explicit(&s->bytes, (unsigned long long)r, memory_order_relaxed);
                if ((size_t)r != size) atomic_fetch_add_explicit(&s->short_ret, 1, memory_order_relaxed);
            }
            amin(&s->min_size, size);
            amax(&s->max_size, size);
            atomic_store_explicit(&s->last_size, size, memory_order_relaxed);
            unsigned long long prev = atomic_exchange_explicit(&s->last_ns, t, memory_order_relaxed);
            if (prev) amax(&s->max_gap_ns, t - prev);
            else atomic_store_explicit(&s->first_ns, t, memory_order_relaxed);
        }
    }
    return r;
}

static void dump_once(void) {
    char b[4096];
    int n = snprintf(b, sizeof(b), "IVANNA PAL WRITE COUNTER v1 pid=%d enabled=%d total_calls=%llu table_full=%llu bad_buf=%llu\n",
                     (int)getpid(), atomic_load(&g_enabled), atomic_load(&g_total_calls),
                     atomic_load(&g_table_full), atomic_load(&g_bad_buf));
    for (int i = 0; i < SLOTS && n > 0 && n < (int)sizeof(b) - 300; ++i) {
        uintptr_t h = atomic_load(&g_slots[i].handle);
        if (!h) continue;
        struct slot *s = &g_slots[i];
        unsigned long long c = atomic_load(&s->calls), mn = atomic_load(&s->min_size), mx = atomic_load(&s->max_size);
        n += snprintf(b + n, sizeof(b) - (size_t)n,
            "STREAM h=0x%lx calls=%llu bytes=%llu min_size=%llu max_size=%llu last_size=%llu short=%llu err=%llu max_gap_us=%.1f span_ms=%.1f\n",
            (unsigned long)h, c, atomic_load(&s->bytes), mn, mx, atomic_load(&s->last_size),
            atomic_load(&s->short_ret), atomic_load(&s->err_ret), atomic_load(&s->max_gap_ns) / 1e3,
            (atomic_load(&s->last_ns) - atomic_load(&s->first_ns)) / 1e6);
    }
    int fd = open(IVANNA_OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        if (n > 0) (void)!write(fd, b, (size_t)n);
        (void)fchmod(fd, 0644);
        close(fd);
    }
    LOGI("calls=%llu streams_written", atomic_load(&g_total_calls));
}
static void *dump_thread(void *a) {
    (void)a;
    for (;;) {
        struct timespec ts = { IVANNA_DUMP_PERIOD_S, 0 };
        nanosleep(&ts, NULL);
        atomic_store(&g_enabled, access(IVANNA_OFF_PATH, F_OK) != 0);
        dump_once();
    }
    return NULL;
}
__attribute__((constructor)) static void ivanna_counter_init(void) {
    pthread_attr_t at;
    pthread_t th;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 64 * 1024);
    if (pthread_create(&th, &at, dump_thread, NULL) != 0) LOGI("dump thread not started");
    pthread_attr_destroy(&at);
    LOGI("loaded pid=%d out=%s", (int)getpid(), IVANNA_OUT_PATH);
}
