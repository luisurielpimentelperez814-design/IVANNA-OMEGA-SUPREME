// ivanna_asrc_bridge.cpp — la MANO enchufada al efecto. Sin excepciones, sin
// new plano en el camino de audio, sin llamadas bloqueantes.
#include "ivanna_asrc_bridge.h"

#include "../asrc/ivanna_asrc_core.h"
#include "../asrc/ivanna_asrc_ipc.h"

#include <atomic>
#include <cstring>
#include <new>

namespace ivanna_asrc_bridge {

namespace {

using ivanna::asrc::Core;
using ivanna::asrc::Reader;
using ivanna::asrc::Writer;

std::string g_ratio_path = ivanna::asrc::kDefaultShm;
std::string g_frames_path = "/data/adb/ivanna_omega/asrc_frames.shm";
std::string g_kill_path = ivanna::asrc::kDefaultKillSwitch;
std::string g_enable_path = "/data/adb/ivanna_omega/asrc.enable";

std::atomic<bool> g_attempted{false};
Core* g_core = nullptr;
Reader* g_ratio_reader = nullptr;
Writer* g_frames_writer = nullptr;

std::atomic<int64_t> g_frames{0};
std::atomic<uint32_t> g_updates{0};
std::atomic<uint32_t> g_fallbacks{0};
std::atomic<uint32_t> g_applied_ratio_bits{0u};

const char* g_reason = "sin_init";

// Un solo intento, con nothrow. Si algo falla, passthrough para siempre y
// audioserver sigue vivo (§4.4 y §5.4).
void tryInitOnce() {
    bool expected = false;
    if (!g_attempted.compare_exchange_strong(expected, true)) return;

    Core* core = new (std::nothrow) Core();
    if (!core) { g_reason = "sin_memoria_core"; return; }
    if (!core->init()) { delete core; g_reason = "init_core_fallo"; return; }

    Reader* rd = new (std::nothrow) Reader();
    Writer* wr = new (std::nothrow) Writer();
    if (!rd || !wr) {
        delete rd; delete wr; delete core;
        g_reason = "sin_memoria_ipc";
        return;
    }
    rd->open(g_ratio_path, 200);
    if (!wr->open(g_frames_path)) {
        // Sin canal de telemetría se puede seguir: el servo verá "sin datos"
        // y publicará 1.0. No es motivo para renunciar al resampler.
        g_reason = "sin_canal_frames";
    }

    g_core = core;
    g_ratio_reader = rd;
    g_frames_writer = wr;
    if (g_reason[0] == 's' && std::strcmp(g_reason, "sin_canal_frames") != 0)
        g_reason = "listo";
}

void publish(double ratio, uint32_t flags, int64_t frames) {
    if (!g_frames_writer) return;
    g_frames_writer->write(ratio, (ratio - 1.0) * 1e6, flags, frames,
                           ivanna::asrc::monotonicNs());
    g_updates.fetch_add(1);
}

}  // namespace

void configure(const std::string& ratio_path, const std::string& frames_path,
               const std::string& kill_switch_path) {
    shutdown();
    g_ratio_path = ratio_path;
    g_frames_path = frames_path;
    g_kill_path = kill_switch_path;
}

// REQUISITO DE ACEPTACIÓN (§8 del encargo: "si una pieza mide como ineficaz, se
// reporta y queda fuera o en bypass. No se fuerza").
// El camino polifásico sólo se aplica si existe el archivo asrc.enable. Por
// defecto NO existe -> ratio 1.0 -> passthrough bit-exacto.
// Motivo medido: a ratios POSITIVOS el THD+N medido es -26.6 dB (no pasa la
// barra de -70 dB) por un artefacto de frontera de bloque que NO se ha
// resuelto. A ratios negativos mide -70.4 dB y la corrección de deriva queda
// en ±2 ppm. Con el defecto abierto, la pieza queda EN BYPASS, no se fuerza.
bool polyphaseEnabled() {
    return ivanna::asrc::killSwitchPresent(g_enable_path);
}

size_t process(int16_t* buf, size_t frames, int channels, int rate) {
    (void)rate;
    if (!buf || frames == 0) return frames;
    if (channels < 1 || channels > 2) channels = 2;

    tryInitOnce();
    if (!g_core) { g_reason = "sin_core_passthrough"; return frames; }

    // Kill switch: se consulta en cada bloque, es un stat() de una ruta.
    if (ivanna::asrc::killSwitchPresent(g_kill_path)) {
        g_reason = "kill_switch";
        g_core->setBypass(true);
        const int64_t f = g_frames.fetch_add(static_cast<int64_t>(frames)) + frames;
        publish(1.0, ivanna::asrc::kFlagKillSwitch | ivanna::asrc::kFlagDaemonAlive, f);
        return frames;
    }
    // Sin asrc.enable -> bypass. Ver polyphaseEnabled().
    g_core->setBypass(!polyphaseEnabled());

    double ratio = 1.0;
    bool fresh = false;
    if (g_ratio_reader) {
        const ivanna::asrc::ReadResult r = g_ratio_reader->read();
        g_reason = r.reason;
        if (r.fresh) { ratio = r.ratio; fresh = true; }
    } else {
        g_reason = "sin_reader";
    }
    if (!fresh) g_fallbacks.fetch_add(1);

    const double applied = g_core->setRatio(ratio);
    uint32_t bits = 0;
    std::memcpy(&bits, &applied, sizeof(bits));
    g_applied_ratio_bits.store(bits);

    if (applied == 1.0) {
        g_core->processS16(buf, frames, buf, frames, channels);
        const int64_t f = g_frames.fetch_add(static_cast<int64_t>(frames)) + frames;
        publish(1.0, ivanna::asrc::kFlagDaemonAlive, f);
        return frames;
    }

    // Camino con ratio: bloque de salida estático (sin asignar en el camino de
    // audio). El tamaño cubre cualquier bloque de AudioFlinger.
    constexpr size_t kChunk = 8192;
    static int16_t scratch[kChunk * 2];
    size_t done_in = 0;
    size_t done_out = 0;
    while (done_in < frames) {
        size_t n = frames - done_in;
        if (n > kChunk) n = kChunk;
        const size_t produced = g_core->processS16(
            buf + done_in * channels, n, scratch, kChunk, channels).out_frames;
        if (produced == 0) break;
        std::memcpy(buf + done_out * channels, scratch,
                    produced * channels * sizeof(int16_t));
        done_in += n;
        done_out += produced;
    }
    if (done_out == 0) {
        // No se produjo nada (bloque más corto que el contexto del FIR): se
        // deja el audio tal cual. Jamás silencio por un fallo del resampler.
        g_reason = "produccion_cero_passthrough";
        const int64_t f = g_frames.fetch_add(static_cast<int64_t>(frames)) + frames;
        publish(1.0, ivanna::asrc::kFlagDaemonAlive, f);
        return frames;
    }
    const int64_t f = g_frames.fetch_add(static_cast<int64_t>(done_out)) + done_out;
    publish(applied, ivanna::asrc::kFlagDaemonAlive, f);
    return done_out;
}

Telemetry telemetry() {
    Telemetry t;
    t.frames_written = g_frames.load();
    t.last_call_ns = ivanna::asrc::monotonicNs();
    uint32_t bits = g_applied_ratio_bits.load();
    std::memcpy(&t.applied_ratio, &bits, sizeof(bits));
    t.passthrough = (t.applied_ratio == 1.0);
    t.ratio_updates = g_updates.load();
    t.fallback_events = g_fallbacks.load();
    return t;
}

const char* lastReason() { return g_reason; }

void shutdown() {
    delete g_frames_writer; g_frames_writer = nullptr;
    delete g_ratio_reader;  g_ratio_reader = nullptr;
    delete g_core;          g_core = nullptr;
    g_attempted.store(false);
    g_reason = "shutdown";
}

}  // namespace ivanna_asrc_bridge
