// ============================================================================
// ivanna_asrcd.cpp — el DAEMON. Cerebro del ASRC. v2.5.396
//
// Proceso userspace aparte. NO se inyecta en nada, NO toca el HAL, NO usa
// LD_PRELOAD, NO depende de .rc ni de init. Lo arranca service.sh, que es el
// mecanismo que Magisk SÍ ejecuta (verificado: los módulos se montan y
// service.sh corre en late_start service, DESPUÉS de que init haya parseado
// /vendor/etc/init/ — que es justo lo que hacía imposible la vía .rc).
//
// Cada 20 ms: lee frames del efecto, alimenta Theil-Sen, publica ratio con
// slew de 5 ppm. Fail-closed: sin datos frescos publica 1.0, y si el daemon
// muere el bloque envejece y el efecto cae a 1.0 por timeout.
// ============================================================================
#include "../asrcd/ivanna_servo.h"
#include "../asrc/ivanna_asrc_ipc.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

using ivanna::asrc::Block;
using ivanna::asrc::kFlagDaemonAlive;
using ivanna::asrc::kFlagKillSwitch;
using ivanna::asrc::killSwitchPresent;
using ivanna::asrc::monotonicNs;
using ivanna::asrcd::TheilSenServo;

std::atomic<bool> g_run{true};
void onSignal(int) { g_run.store(false); }

// Criterio único de validez (el mismo que el efecto y el Core, §6.4).
bool validRatio(double r) {
    if (!std::isfinite(r)) return false;
    return std::fabs((r - 1.0) * 1e6) <= 500.0 + 1e-9;
}

// Log con límite de frecuencia (§7.1): máximo una línea cada 5 s por tipo.
struct RateLimitedLog {
    int64_t last_ns = 0;
    bool allow() {
        const int64_t now = monotonicNs();
        if (now - last_ns < 5'000'000'000LL) return false;
        last_ns = now;
        return true;
    }
};

}  // namespace

int main(int argc, char** argv) {
    std::string ratio_path = ivanna::asrc::kDefaultShm;
    std::string frames_path = "/data/adb/ivanna_omega/asrc_frames.shm";
    std::string kill_path = ivanna::asrc::kDefaultKillSwitch;
    int period_ms = 20;
    bool verbose = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--ratio" && i + 1 < argc) ratio_path = argv[++i];
        else if (a == "--frames" && i + 1 < argc) frames_path = argv[++i];
        else if (a == "--kill" && i + 1 < argc) kill_path = argv[++i];
        else if (a == "--period-ms" && i + 1 < argc) period_ms = std::atoi(argv[++i]);
        else if (a == "--verbose") verbose = true;
        else if (a == "--help") {
            std::printf("ivanna_asrcd [--ratio F] [--frames F] [--kill F] "
                        "[--period-ms N] [--verbose]\n");
            return 0;
        }
    }
    if (period_ms < 5) period_ms = 5;
    if (period_ms > 1000) period_ms = 1000;

    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGPIPE, SIG_IGN);

    ivanna::asrc::Writer ratio_w;
    if (!ratio_w.open(ratio_path)) {
        std::fprintf(stderr, "ivanna_asrcd: no puedo abrir %s: %s\n",
                     ratio_path.c_str(), std::strerror(errno));
        return 2;
    }
    ivanna::asrc::Reader frames_r;
    frames_r.open(frames_path, 500);

    TheilSenServo servo(48, 48000.0, 500.0);
    double current_ratio = 1.0;
    double last_ppm = 0.0;
    int64_t last_frames = -1;
    int64_t last_add_ns = 0;
    uint32_t published = 0;
    RateLimitedLog log_no_frames, log_conv, log_pub;

    std::printf("ivanna_asrcd: ratio=%s frames=%s kill=%s period=%dms\n",
                ratio_path.c_str(), frames_path.c_str(), kill_path.c_str(),
                period_ms);
    std::fflush(stdout);

    while (g_run.load()) {
        if (killSwitchPresent(kill_path)) {
            ratio_w.write(1.0, 0.0, kFlagKillSwitch | kFlagDaemonAlive, 0,
                          monotonicNs());
            std::printf("ivanna_asrcd: kill switch (%s) -> ratio 1.0, saliendo\n",
                        kill_path.c_str());
            std::fflush(stdout);
            return 0;
        }

        // 1-2. Telemetría del efecto (frames + instante), validada por seqlock.
        Block b{};
        const bool have = frames_r.readBlock(&b);
        if (have) {
            const int64_t age = monotonicNs() - b.ts_ns;
            // Decimación a 500 ms: el contador de frames es entero y con
            // 20 ms de base 1 frame = 1041 ppm de cuantización; con 500 ms son
            // 41.7 ppm y con la ventana de 48 muestras (~24 s) el error baja
            // por debajo de 1 ppm. El lazo de slew (5 ppm/actualización) cubre
            // la latencia de la decimación.
            const int64_t min_spacing = 500'000'000LL;
            if (age >= 0 && age < 1'000'000'000LL && b.frames != last_frames &&
                (last_add_ns == 0 || b.ts_ns - last_add_ns >= min_spacing)) {
                servo.add(b.ts_ns, b.frames);
                last_frames = b.frames;
                last_add_ns = b.ts_ns;
            }
        } else if (log_no_frames.allow()) {
            std::printf("ivanna_asrcd: sin telemetria de frames -> ratio 1.0\n");
            std::fflush(stdout);
        }

        // 3. Servo.
        double ppm = 0.0;
        if (servo.estimate(&ppm)) {
            last_ppm = ppm;
            if (verbose && log_conv.allow()) {
                std::printf("ivanna_asrcd: ppm=%+.3f ratio=%.9f muestras=%zu\n",
                            ppm, TheilSenServo::ratioFor(ppm), servo.samples());
                std::fflush(stdout);
            }
        }

        double target = servo.ready() ? TheilSenServo::ratioFor(last_ppm) : 1.0;
        if (!validRatio(target)) target = 1.0;
        current_ratio = TheilSenServo::slew(current_ratio, target, 5.0);
        if (!validRatio(current_ratio)) current_ratio = 1.0;

        // 4. Publicar.
        const double ppm_out = servo.ready() ? last_ppm : 0.0;
        if (ratio_w.write(current_ratio, ppm_out, kFlagDaemonAlive,
                          last_frames < 0 ? 0 : last_frames, monotonicNs())) {
            ++published;
        } else if (log_pub.allow()) {
            std::printf("ivanna_asrcd: no pude publicar el ratio\n");
            std::fflush(stdout);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
    }

    ratio_w.write(1.0, 0.0, kFlagDaemonAlive, 0, monotonicNs());
    std::printf("ivanna_asrcd: salida limpia tras %u publicaciones, ratio 1.0\n",
                published);
    return 0;
}
