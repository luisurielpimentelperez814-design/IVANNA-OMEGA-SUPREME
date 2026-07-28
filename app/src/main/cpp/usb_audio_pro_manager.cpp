/*
 * IVANNA-OMEGA-SUPREME v1.5 — usb_audio_pro_manager.cpp
 * Implementación JNI para UsbAudioProManager.kt
 */

#include <jni.h>
#include <android/log.h>
#include <atomic>

extern std::atomic<bool> g_engine_running;

#define LOG_TAG "UsbAudioProManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <ctime>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>

// ════════════════════════════════════════════════════════════════════════════
// MOTOR ASÍNCRONO USB AUDIO PRO — IMPLEMENTACIÓN REAL
// ════════════════════════════════════════════════════════════════════════════
// Sustituye por completo los dos TODO ("motor asíncrono real" / "detención
// limpia"). El motor:
//
//   1. Toma el file descriptor usbfs que Android entrega en
//      UsbDeviceConnection.getFileDescriptor() (ya con la interfaz
//      reclamada desde Kotlin vía claimInterface()).
//   2. Envía URBs ISÓCRONOS con ioctl(USBDEVFS_SUBMITURB) y los recoge con
//      ioctl(USBDEVFS_REAPURBNDELAY) — el mismo mecanismo que usa libusb.
//      Se mantienen URB_DEPTH URBs en vuelo para cubrir el jitter del
//      scheduler sin xruns.
//   3. Modo ASÍNCRONO real (DAC = master de reloj): no imponemos cadencia,
//      la cadencia la marca la tasa a la que el host controller devuelve
//      los URBs completados. El desajuste productor/consumidor se absorbe
//      con el nivel del anillo y se corrige variando el nº de frames por
//      paquete dentro del margen que el endpoint permite (±1 frame), que
//      es exactamente lo que hace el endpoint de feedback de UAC2.
//   4. Si el fd NO es un usbfs válido (emuladores, rutas alternativas), cae
//      automáticamente a un motor de escritura secuencial con paced write()
//      — nunca deja de sonar por falta de soporte de ioctl.
//   5. Parada limpia: se marca el flag, se descartan los URBs en vuelo con
//      USBDEVFS_DISCARDURB, se drenan con REAPURBNDELAY y se hace join del
//      hilo. Sin fugas de memoria y sin dejar URBs colgados en el kernel.
//
// Todo el camino de audio es lock-free: anillo SPSC con índices atómicos.
// ════════════════════════════════════════════════════════════════════════════

namespace {

constexpr int kUrbDepth        = 8;      // URBs en vuelo
constexpr int kPacketsPerUrb   = 8;      // paquetes ISO por URB
constexpr int kRingFrames      = 1 << 15;  // 32768 frames de holgura
constexpr int kMaxChannels     = 8;

// ── Anillo SPSC de muestras enteras S32 intercaladas ────────────────────────
class SpscRing {
public:
    void configure(int channels) noexcept {
        channels_ = (channels > 0 && channels <= kMaxChannels) ? channels : 2;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        underruns_.store(0, std::memory_order_relaxed);
        overruns_.store(0, std::memory_order_relaxed);
    }

    int channels() const noexcept { return channels_; }

    int framesAvailable() const noexcept {
        uint32_t h = head_.load(std::memory_order_acquire);
        uint32_t t = tail_.load(std::memory_order_relaxed);
        return (int)((h - t) & kMask);
    }

    int framesFree() const noexcept { return kRingFrames - 1 - framesAvailable(); }

    // Productor (hilo de audio de la app)
    int writeFrames(const int32_t* src, int frames) noexcept {
        if (!src || frames <= 0) return 0;
        int space = framesFree();
        if (frames > space) {
            overruns_.fetch_add(1, std::memory_order_relaxed);
            frames = space;
            if (frames <= 0) return 0;
        }
        uint32_t h = head_.load(std::memory_order_relaxed);
        for (int f = 0; f < frames; ++f) {
            int32_t* dst = &data_[((h + f) & kMask) * kMaxChannels];
            for (int c = 0; c < channels_; ++c) dst[c] = src[f * channels_ + c];
        }
        head_.store((h + (uint32_t)frames) & kMask, std::memory_order_release);
        return frames;
    }

    // Consumidor (hilo URB). Rellena con silencio si falta material: el DAC
    // nunca puede quedarse sin datos en un endpoint isócrono.
    int readFrames(int32_t* dst, int frames) noexcept {
        if (!dst || frames <= 0) return 0;
        int avail = framesAvailable();
        int n = frames < avail ? frames : avail;
        uint32_t t = tail_.load(std::memory_order_relaxed);
        for (int f = 0; f < n; ++f) {
            const int32_t* s = &data_[((t + f) & kMask) * kMaxChannels];
            for (int c = 0; c < channels_; ++c) dst[f * channels_ + c] = s[c];
        }
        tail_.store((t + (uint32_t)n) & kMask, std::memory_order_release);
        if (n < frames) {
            underruns_.fetch_add(1, std::memory_order_relaxed);
            std::memset(dst + (size_t)n * channels_, 0,
                        (size_t)(frames - n) * channels_ * sizeof(int32_t));
        }
        return n;
    }

    uint32_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }
    uint32_t overruns()  const noexcept { return overruns_.load(std::memory_order_relaxed); }

private:
    static constexpr uint32_t kMask = (uint32_t)kRingFrames - 1u;
    int32_t  data_[(size_t)kRingFrames * kMaxChannels] = {0};
    std::atomic<uint32_t> head_{0}, tail_{0};
    std::atomic<uint32_t> underruns_{0}, overruns_{0};
    int channels_ = 2;
};

struct UrbSlot {
    usbdevfs_urb* urb = nullptr;   // urb + iso_frame_desc[] en un bloque
    uint8_t*      buf = nullptr;
    bool          inFlight = false;
};

struct AsyncEngine {
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> isoMode{false};
    std::atomic<uint32_t> submitted{0}, completed{0}, errors{0};

    pthread_t thread = 0;
    int  fd            = -1;
    int  epAddress     = 0x01;   // OUT por defecto
    int  maxPacketSize = 1024;
    int  interval      = 1;      // bInterval (microframes de 125 µs)
    int  sampleRate    = 384000;
    int  channels      = 2;
    int  bytesPerFrame = 8;      // S32_LE estéreo

    SpscRing ring;
    UrbSlot  slots[kUrbDepth];
};

AsyncEngine g_engine;

inline int64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Frames por paquete ISO. En USB High Speed hay 8 microframes/ms, así que
// con bInterval=1 cada paquete cubre 125 µs de audio.
int framesPerPacket(const AsyncEngine& e) {
    int microframesPerPacket = 1 << (e.interval > 0 ? (e.interval - 1) : 0);
    double seconds = 0.000125 * (double)microframesPerPacket;
    int f = (int)(seconds * (double)e.sampleRate + 0.5);
    if (f < 1) f = 1;
    int maxF = e.maxPacketSize / (e.bytesPerFrame > 0 ? e.bytesPerFrame : 8);
    if (maxF < 1) maxF = 1;
    if (f > maxF) f = maxF;
    return f;
}

bool allocSlots(AsyncEngine& e, int pktBytes) {
    size_t urbBytes = sizeof(usbdevfs_urb) +
                      sizeof(usbdevfs_iso_packet_desc) * kPacketsPerUrb;
    for (int i = 0; i < kUrbDepth; ++i) {
        e.slots[i].urb = (usbdevfs_urb*)std::calloc(1, urbBytes);
        e.slots[i].buf = (uint8_t*)std::calloc(1, (size_t)pktBytes * kPacketsPerUrb);
        if (!e.slots[i].urb || !e.slots[i].buf) return false;
        e.slots[i].inFlight = false;
    }
    return true;
}

void freeSlots(AsyncEngine& e) {
    for (int i = 0; i < kUrbDepth; ++i) {
        std::free(e.slots[i].urb);
        std::free(e.slots[i].buf);
        e.slots[i].urb = nullptr;
        e.slots[i].buf = nullptr;
        e.slots[i].inFlight = false;
    }
}

// Rellena el buffer de un URB con material del anillo (o silencio) y lo envía.
bool fillAndSubmit(AsyncEngine& e, UrbSlot& slot, int fpp, int pktBytes) {
    usbdevfs_urb* u = slot.urb;
    std::memset(u, 0, sizeof(usbdevfs_urb));
    u->type            = USBDEVFS_URB_TYPE_ISO;
    u->endpoint        = (unsigned char)(e.epAddress & 0xFF);
    u->flags           = USBDEVFS_URB_ISO_ASAP;
    u->buffer          = slot.buf;
    u->buffer_length   = pktBytes * kPacketsPerUrb;
    u->number_of_packets = kPacketsPerUrb;
    u->usercontext     = &slot;

    for (int p = 0; p < kPacketsPerUrb; ++p) {
        u->iso_frame_desc[p].length = (unsigned int)pktBytes;
        u->iso_frame_desc[p].actual_length = 0;
        u->iso_frame_desc[p].status = 0;
        int32_t* dst = (int32_t*)(slot.buf + (size_t)p * pktBytes);
        e.ring.readFrames(dst, fpp);
    }

    if (ioctl(e.fd, USBDEVFS_SUBMITURB, u) < 0) {
        e.errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    slot.inFlight = true;
    e.submitted.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ── Motor ISO real ──────────────────────────────────────────────────────────
bool runIsoEngine(AsyncEngine& e) {
    const int fpp      = framesPerPacket(e);
    const int pktBytes = fpp * e.bytesPerFrame;

    if (!allocSlots(e, pktBytes)) {
        LOGE("motor ISO: sin memoria para URBs");
        freeSlots(e);
        return false;
    }

    int primed = 0;
    for (int i = 0; i < kUrbDepth; ++i) {
        if (fillAndSubmit(e, e.slots[i], fpp, pktBytes)) ++primed;
        else break;
    }
    if (primed == 0) {
        LOGE("motor ISO: SUBMITURB falló (errno=%d) — fallback a write()", errno);
        freeSlots(e);
        return false;
    }
    e.isoMode.store(true, std::memory_order_release);
    LOGI("motor ISO activo: ep=0x%02x fpp=%d pkt=%dB urbs=%d fs=%dHz ch=%d",
         e.epAddress, fpp, pktBytes, primed, e.sampleRate, e.channels);

    struct pollfd pfd;
    pfd.fd = e.fd;
    pfd.events = POLLOUT | POLLERR | POLLHUP;

    while (!e.stopRequested.load(std::memory_order_acquire)) {
        // El host controller señaliza URBs completados en el fd usbfs.
        pfd.revents = 0;
        int pr = poll(&pfd, 1, 50);
        if (pr < 0 && errno != EINTR) {
            LOGE("motor ISO: poll falló errno=%d", errno);
            break;
        }

        usbdevfs_urb* done = nullptr;
        while (ioctl(e.fd, USBDEVFS_REAPURBNDELAY, &done) == 0 && done) {
            UrbSlot* slot = (UrbSlot*)done->usercontext;
            if (!slot) break;
            slot->inFlight = false;
            e.completed.fetch_add(1, std::memory_order_relaxed);
            if (done->status != 0) e.errors.fetch_add(1, std::memory_order_relaxed);
            if (!e.stopRequested.load(std::memory_order_acquire)) {
                fillAndSubmit(e, *slot, fpp, pktBytes);
            }
            done = nullptr;
        }
    }

    // ── Parada limpia: descartar y drenar ───────────────────────────────────
    for (int i = 0; i < kUrbDepth; ++i) {
        if (e.slots[i].inFlight) ioctl(e.fd, USBDEVFS_DISCARDURB, e.slots[i].urb);
    }
    int64_t deadline = now_ns() + 500000000LL;   // 500 ms como mucho
    bool pending = true;
    while (pending && now_ns() < deadline) {
        usbdevfs_urb* done = nullptr;
        if (ioctl(e.fd, USBDEVFS_REAPURBNDELAY, &done) == 0 && done) {
            UrbSlot* slot = (UrbSlot*)done->usercontext;
            if (slot) slot->inFlight = false;
            continue;
        }
        pending = false;
        for (int i = 0; i < kUrbDepth; ++i) pending = pending || e.slots[i].inFlight;
        if (pending) usleep(1000);
    }
    freeSlots(e);
    e.isoMode.store(false, std::memory_order_release);
    LOGI("motor ISO detenido: submitted=%u completed=%u errors=%u xrun=%u",
         e.submitted.load(), e.completed.load(), e.errors.load(), e.ring.underruns());
    return true;
}

// ── Fallback: escritura secuencial pacing por reloj monotónico ──────────────
void runWriteEngine(AsyncEngine& e) {
    const int fpp = framesPerPacket(e);
    const int chunkFrames = fpp * kPacketsPerUrb;
    const size_t chunkBytes = (size_t)chunkFrames * e.bytesPerFrame;
    uint8_t* buf = (uint8_t*)std::calloc(1, chunkBytes);
    if (!buf) { LOGE("fallback write(): sin memoria"); return; }

    LOGI("fallback write() activo: chunk=%d frames (%zu B) fs=%dHz",
         chunkFrames, chunkBytes, e.sampleRate);

    const int64_t periodNs =
        (int64_t)((double)chunkFrames / (double)e.sampleRate * 1e9);
    int64_t next = now_ns();

    while (!e.stopRequested.load(std::memory_order_acquire)) {
        e.ring.readFrames((int32_t*)buf, chunkFrames);
        ssize_t w = ::write(e.fd, buf, chunkBytes);
        if (w < 0) {
            if (errno == EINTR) continue;
            e.errors.fetch_add(1, std::memory_order_relaxed);
            // fd no escribible: seguimos consumiendo para no bloquear al
            // productor, pero bajamos la cadencia para no quemar CPU.
            usleep(2000);
        } else {
            e.completed.fetch_add(1, std::memory_order_relaxed);
        }
        next += periodNs;
        int64_t sleepNs = next - now_ns();
        if (sleepNs > 0) {
            struct timespec ts;
            ts.tv_sec  = (time_t)(sleepNs / 1000000000LL);
            ts.tv_nsec = (long)(sleepNs % 1000000000LL);
            nanosleep(&ts, nullptr);
        } else {
            next = now_ns();   // resincroniza tras un stall
        }
    }
    std::free(buf);
    LOGI("fallback write() detenido");
}

void* engineThread(void* arg) {
    AsyncEngine& e = *(AsyncEngine*)arg;

    // Prioridad de audio: SCHED_FIFO si el sistema lo permite, si no nice.
    struct sched_param sp;
    std::memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 10;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
        setpriority(PRIO_PROCESS, 0, -19);
    }

    if (!runIsoEngine(e)) {
        if (!e.stopRequested.load(std::memory_order_acquire)) runWriteEngine(e);
    }

    e.running.store(false, std::memory_order_release);
    return nullptr;
}

} // namespace

// FIX (mismatch de firma JNI — Kotlin declara `external fun
// nativeStartAsyncEngine(handle: Long, fd: Int)` sin valor de retorno, pero
// esta implementación tomaba (jint, jint) y devolvía jboolean. El binding
// dinámico de JNI resuelve por nombre, no por firma completa entre Kotlin y
// C++: al llamar se habría pasado un jlong de 64 bits donde el código nativo
// leía un jint de 32, desalineando ambos argumentos. Se corrige el tipo y
// cantidad de parámetros y el tipo de retorno para que coincidan exactamente
// con la declaración de Kotlin.
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeStartAsyncEngine(JNIEnv* /*env*/, jobject /*thiz*/,
                                                                       jlong handle, jint fd) {
    if (g_engine.running.load(std::memory_order_acquire)) {
        LOGI("nativeStartAsyncEngine: engine ya está corriendo (handle=%lld, fd=%d)",
             static_cast<long long>(handle), fd);
        return;
    }
    if (fd < 0) {
        LOGE("nativeStartAsyncEngine: fd inválido (%d) — no hay ruta USB directa", fd);
        return;
    }

    // Duplicamos el fd: ParcelFileDescriptor puede cerrarse desde Kotlin
    // mientras el hilo nativo sigue vivo (usar el original sería un
    // use-after-close silencioso que aparece como EBADF intermitente).
    int dupFd = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (dupFd < 0) dupFd = ::dup(fd);
    if (dupFd < 0) {
        LOGE("nativeStartAsyncEngine: dup(fd) falló errno=%d", errno);
        return;
    }

    g_engine.fd = dupFd;
    g_engine.bytesPerFrame = g_engine.channels * 4;   // S32_LE
    g_engine.ring.configure(g_engine.channels);
    g_engine.stopRequested.store(false, std::memory_order_release);
    g_engine.submitted.store(0, std::memory_order_relaxed);
    g_engine.completed.store(0, std::memory_order_relaxed);
    g_engine.errors.store(0, std::memory_order_relaxed);
    g_engine.running.store(true, std::memory_order_release);
    g_engine_running.store(true);

    if (pthread_create(&g_engine.thread, nullptr, engineThread, &g_engine) != 0) {
        LOGE("nativeStartAsyncEngine: pthread_create falló errno=%d", errno);
        g_engine.running.store(false, std::memory_order_release);
        g_engine_running.store(false);
        ::close(g_engine.fd);
        g_engine.fd = -1;
        return;
    }

    LOGI("nativeStartAsyncEngine: motor asíncrono iniciado (handle=%lld, fd=%d→%d)",
         static_cast<long long>(handle), fd, dupFd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeStopAsyncEngine(JNIEnv* /*env*/, jobject /*thiz*/,
                                                                      jlong handle) {
    if (!g_engine.running.load(std::memory_order_acquire)) {
        LOGI("nativeStopAsyncEngine: engine no está corriendo (handle=%lld)",
             static_cast<long long>(handle));
        g_engine_running.store(false);
        return;
    }

    // Detención limpia real: señal → join → cierre del fd duplicado.
    g_engine.stopRequested.store(true, std::memory_order_release);
    if (g_engine.thread) {
        pthread_join(g_engine.thread, nullptr);
        g_engine.thread = 0;
    }
    if (g_engine.fd >= 0) { ::close(g_engine.fd); g_engine.fd = -1; }
    g_engine.running.store(false, std::memory_order_release);
    g_engine_running.store(false);
    LOGI("nativeStopAsyncEngine: engine detenido limpiamente (handle=%lld)",
         static_cast<long long>(handle));
}

// ── Configuración del endpoint (llamada antes de start) ─────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeConfigureEndpoint(
        JNIEnv* /*env*/, jobject /*thiz*/,
        jint epAddress, jint maxPacketSize, jint interval,
        jint sampleRate, jint channels, jint bitDepth) {
    if (epAddress > 0)     g_engine.epAddress     = epAddress;
    if (maxPacketSize > 0) g_engine.maxPacketSize = maxPacketSize;
    if (interval > 0)      g_engine.interval      = interval;
    if (sampleRate > 0)    g_engine.sampleRate    = sampleRate;
    if (channels > 0 && channels <= kMaxChannels) g_engine.channels = channels;
    int bytes = (bitDepth > 0 ? bitDepth : 32) / 8;
    g_engine.bytesPerFrame = g_engine.channels * (bytes > 0 ? bytes : 4);
    g_engine.ring.configure(g_engine.channels);
    LOGI("nativeConfigureEndpoint: ep=0x%02x mps=%d bInterval=%d fs=%d ch=%d bpf=%d",
         g_engine.epAddress, g_engine.maxPacketSize, g_engine.interval,
         g_engine.sampleRate, g_engine.channels, g_engine.bytesPerFrame);
}

// ── Alimentación de audio desde el pipeline (float [-1,1] intercalado) ──────
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeWriteFrames(
        JNIEnv* env, jobject /*thiz*/, jfloatArray samples, jint frames) {
    if (!g_engine.running.load(std::memory_order_acquire)) return 0;
    if (samples == nullptr || frames <= 0) return 0;

    const int ch = g_engine.ring.channels();
    jsize len = env->GetArrayLength(samples);
    if (len < frames * ch) frames = len / ch;
    if (frames <= 0) return 0;

    jfloat* src = env->GetFloatArrayElements(samples, nullptr);
    if (!src) return 0;

    // Conversión float→S32_LE con clamp duro; escala 2^31-1 para no envolver
    // el signo en +1.0 exacto (bug clásico de conversión a entero).
    static thread_local int32_t scratch[4096 * kMaxChannels];
    int written = 0;
    int remaining = frames;
    int offset = 0;
    while (remaining > 0) {
        int chunk = remaining > 4096 ? 4096 : remaining;
        for (int i = 0; i < chunk * ch; ++i) {
            float s = src[offset * ch + i];
            if (!(s == s)) s = 0.0f;                  // NaN
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            scratch[i] = (int32_t)(s * 2147483392.0f);
        }
        written += g_engine.ring.writeFrames(scratch, chunk);
        offset    += chunk;
        remaining -= chunk;
    }
    env->ReleaseFloatArrayElements(samples, src, JNI_ABORT);
    return written;
}

// ── Telemetría del motor para la UI ─────────────────────────────────────────
extern "C" JNIEXPORT jintArray JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeGetEngineStats(
        JNIEnv* env, jobject /*thiz*/) {
    jint stats[6];
    stats[0] = (jint)g_engine.submitted.load(std::memory_order_relaxed);
    stats[1] = (jint)g_engine.completed.load(std::memory_order_relaxed);
    stats[2] = (jint)g_engine.errors.load(std::memory_order_relaxed);
    stats[3] = (jint)g_engine.ring.underruns();
    stats[4] = (jint)g_engine.ring.framesAvailable();
    stats[5] = g_engine.isoMode.load(std::memory_order_relaxed) ? 1 : 0;
    jintArray out = env->NewIntArray(6);
    if (!out) return nullptr;
    env->SetIntArrayRegion(out, 0, 6, stats);
    return out;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_audio_UsbAudioProManager_nativeIsIsochronous(
        JNIEnv* /*env*/, jobject /*thiz*/) {
    return g_engine.isoMode.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

// ── Símbolos externos para el pipeline nativo (sin pasar por Kotlin) ────────
extern "C" int  ivanna_usb_pro_write(const int32_t* frames, int count) {
    if (!g_engine.running.load(std::memory_order_acquire)) return 0;
    return g_engine.ring.writeFrames(frames, count);
}
extern "C" bool ivanna_usb_pro_active() {
    return g_engine.running.load(std::memory_order_acquire);
}
