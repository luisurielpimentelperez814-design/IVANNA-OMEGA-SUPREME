// ivanna_daemon_v2.cpp — El CEREBRO de la Fusión Magistral (reescritura total).
//
// El daemon es UN proceso propio (root), NO vive dentro del HAL:
//   - Vive en /vendor/bin/ivanna_daemon_v2 (lo instala el módulo).
//   - Lo arranca service.sh en cada arranque (después de los servicios).
//   - Se fusiona con el ASRC (que vive DENTRO del HAL) SOLO a través del
//     estado compartido IvannaFusionState (SHM). Un solo estado, un solo bucle.
//   - NO puede vivir dentro del HAL: el HAL (usuario audioserver, contexto
//     hal_audio_default) NO puede correr dumpsys, NO puede leer la UI y NO
//     puede ser root. Y NO debe: si el cerebro muere, el HAL debe sobrevivir
//     (el músculo pasa a passthrough, el audio sigue).
//
// El cerebro hace (un solo bucle, ~20 Hz):
//   1. Leer las mediciones del músculo desde el estado compartido.
//   2. Correr el servo Kalman de 3 estados (deriva + tasa + aceleración)
//      y calcular el ratio (salida/entrada).
//   3. Certificar el reloj en vivo (mmap vs CLOCK_MONOTONIC) y decidir la
//      fuente de reloj.
//   4. Descubrir la sesión REAL de música (parser de 5 campos, cada 3 s).
//   5. Decidir el veredicto y el control de la cadena de súper efectos
//      (lee /data/adb/ivanna_omega/fusion_fx.conf; por defecto TODO apagado).
//   6. Escribir ratio + veredicto + efectos en el estado compartido.
//   7. Publicar el snapshot NEUTRO (para el efecto, si lo hay) + heartbeat.
//
// Fail-closed: si la SHM no está → no publica; si el músculo no mide →
// veredicto BYPASS; si descubrir la sesión falla (kError) → se conserva la
// sesión anterior; si no hay música (kNone) → sessionId 0. Kill switch:
// /data/vendor/ivanna_audio/pal_asrc.off → fusión en bypass (ratio 1.0,
// efectos off). Si existe /data/adb/ivanna_omega/ivanna_daemon.off, el
// daemon no arranca (service.sh lo respeta).
//
// Compilación: la CI lo compila para arm64-v8a. El módulo lo instala en
// system/bin/ivanna_daemon_v2.

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>

#include "control/daemon_command_responder.h"
#include "daemon/session_discovery.hpp"
#include "include/omega_control_bus.h"
#include "include/psi_core.h"
#include "include/ivanna_fusion_state.h"

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) noexcept { g_running.store(false); }

uint64_t monotonicMs() noexcept {
    struct timespec ts {};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000u
         + static_cast<uint64_t>(ts.tv_nsec) / 1000000u;
}
int64_t monotonicNs() noexcept {
    struct timespec ts {};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void logLine(const char* msg) noexcept {
    std::FILE* f = std::fopen("/data/adb/ivanna_omega/daemon_v2.log", "a");
    if (!f) return;
    std::fprintf(f, "[%llu] %s\n",
                 static_cast<unsigned long long>(monotonicMs()), msg);
    std::fclose(f);
}

// Heartbeat de la fusión (para que la APK lo LEA y se ACOPLE).
// Se escribe en DOS sitios:
//  - /data/adb/ivanna_omega/daemon_v2_heartbeat (diagnóstico, solo root).
//  - /data/local/tmp/ivanna_fusion_heartbeat (LA APK LO LEE: /data/local/tmp
//    sí lo puede leer una app normal; /data/adb NO). Sin este segundo
//    archivo la APK sería ciega a la fusión cuando la Ruta B no está activa
//    (el efecto solo publica al bus local cuando procesa).
// Formato texto clave=valor (como el beacon del efecto): la app lo lee por
// JNI (DSPBridge.nativeFusionState) cada 10 s en AudioBackendSelector.
// fusion_active=1 cuando el veredicto es LOADED (el HAL procesa TODO el
// audio); la app entonces detiene la Ruta A (no procesa dos veces).
void writeHeartbeat(int32_t sessionId, uint64_t generation,
                    float driftPpm, uint32_t verdict, float eta, float psiGamma,
                    float ratio, int32_t clockSource, uint32_t psiActive) noexcept {
    const uint32_t fusionActive = (verdict == IVANNA_VERDICT_LOADED) ? 1u : 0u;
    const char* paths[2] = {"/data/adb/ivanna_omega/daemon_v2_heartbeat",
                            "/data/local/tmp/ivanna_fusion_heartbeat"};
    for (int i = 0; i < 2; ++i) {
        std::FILE* f = std::fopen(paths[i], "w");
        if (!f) continue;
        std::fprintf(f, "alive=1\nts_ms=%llu\nsession_id=%d\ngeneration=%llu\ndrift_ppm=%.2f\nverdict=%u\neta=%.3f\npsi_gamma=%.3f\nfusion_active=%u\npsi_active=%u\nratio=%.6f\nclock=%d\n",
                     static_cast<unsigned long long>(monotonicMs()),
                     static_cast<int>(sessionId),
                     static_cast<unsigned long long>(generation),
                     static_cast<double>(driftPpm), verdict,
                     static_cast<double>(eta), static_cast<double>(psiGamma),
                     fusionActive, psiActive,
                     static_cast<double>(ratio), static_cast<int>(clockSource));
        std::fclose(f);
    }
}

bool fileExists(const char* path) noexcept {
    return ::access(path, F_OK) == 0;
}

// Socket abstracto @omega_daemon_socket (compatibilidad con el módulo y
// la app). service.sh, supervisor.sh y la app (MagiskBridge.kt) deciden
// "SOCKET=CONECTADO" grepeando /proc/net/unix por este nombre, y el
// supervisor relanza el daemon si el proceso corre SIN socket.
//
// v2.5.440 — EL SOCKET YA NO ES SOLO SEÑAL DE VIDA: sirve comandos
// (HELLO + GET_STATUS) en un hilo propio (ver control/
// daemon_command_responder.h). Antes (v2.5.401–439) el cerebro bindeaba
// el socket y el bucle principal drenaba las conexiones con accept()+
// close() inmediato, así que GET_STATUS jamás se respondía y la app no
// podía descubrir la sesión: el efecto omega nunca se acoplaba (Ruta B
// muerta). El hilo de comandos lee el estado vivo (sesión, generación,
// veredicto) de los atómicos que el bucle principal actualiza cada
// iteración; si el hilo muere, la fusión sigue (fail-closed).
int bindDaemonSocket() noexcept {
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr {};
    addr.sun_family = AF_UNIX;
    // Abstracto: sun_path[0] = '\0' + nombre (sin archivo en el fs).
    const char* name = "omega_daemon_socket";
    const size_t nlen = std::strlen(name);
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, nlen);
    const socklen_t len = static_cast<socklen_t>(
        offsetof(struct sockaddr_un, sun_path) + 1 + nlen);
    if (::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), len) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 4) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// ── Servo Kalman de 3 estados ─────────────────────────────────────────────
// Estado: x = [deriva_ppm, tasa_deriva_ppm/s, aceleracion_ppm/s^2].
// Medición: z = deriva instantánea (ppm) desde el reloj del músculo.
// El ratio = 1 + deriva/1e6 (HAL rápido → ratio > 1, ver auditoría riesgo D).
struct Kalman3 {
    double x[3] = {0.0, 0.0, 0.0};  // estado
    double P[3][3] = {{100.0, 0, 0}, {0, 10.0, 0}, {0, 0, 1.0}}; // covarianza
    // Ruido de proceso (Q) y de medición (R). R se adapta: si la medición
    // es ruidosa, el filtro confía más en el modelo (como en la v5).
    double q0 = 0.04, q1 = 0.004, q2 = 0.0004;
    double R = 25.0;
    bool initialized = false;
    double lastInnovation = 0.0;  // |y| de la última actualización (para la pérdida L de η)

    void predict(double dt) noexcept {
        if (dt <= 0.0 || dt > 1.0) dt = 0.05;
        // F = [[1, dt, 0.5*dt^2],[0,1,dt],[0,0,1]]
        double nx0 = x[0] + x[1] * dt + 0.5 * x[2] * dt * dt;
        double nx1 = x[1] + x[2] * dt;
        x[0] = nx0; x[1] = nx1;  // x[2] no cambia
        // P = F*P*F' + Q (Q diagonal). Se hace explícito para 3x3.
        double F[3][3] = {{1, dt, 0.5*dt*dt}, {0, 1, dt}, {0, 0, 1}};
        double FP[3][3] = {};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) FP[i][j] += F[i][k] * P[k][j];
        double FPF[3][3] = {};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k) FPF[i][j] += FP[i][k] * F[j][k];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) P[i][j] = FPF[i][j];
        P[0][0] += q0; P[1][1] += q1; P[2][2] += q2;
    }

    void update(double z) noexcept {
        // H = [1,0,0]. S = P00 + R. K = P[:,0]/S.
        double S = P[0][0] + R;
        if (S <= 0.0) return;
        double K[3] = {P[0][0] / S, P[1][0] / S, P[2][0] / S};
        double y = z - x[0];  // innovación
        lastInnovation = std::fabs(y);
        // Puerta 3σ: si la innovación es absurda, no se traga la medición.
        double sigma = std::sqrt(S);
        if (std::fabs(y) > 3.0 * sigma + 500.0) return;  // outlier
        x[0] += K[0] * y; x[1] += K[1] * y; x[2] += K[2] * y;
        // P = (I - K*H)*P
        for (int j = 0; j < 3; ++j) {
            P[0][j] -= K[0] * P[0][j];
            P[1][j] -= K[1] * P[0][j];
            P[2][j] -= K[2] * P[0][j];
        }
        // R adaptativa: si la innovación es grande, la medición es ruidosa.
        double ay = std::fabs(y);
        R = (ay > 50.0) ? 400.0 : (ay > 10.0 ? 100.0 : 25.0);
        initialized = true;
    }

    double driftPpm() const noexcept { return x[0]; }
    double ratio() const noexcept { return 1.0 + x[0] / 1e6; }
};

// ── Control de efectos (lo lee el cerebro de un archivo de config) ────────
// Formato (una línea por efecto, "nombre=0.0..1.0"): orbe, holos, phi,
// volterra, supreme, npe, reality, eq, limiter (0/1). Por defecto TODO 0
// (neutro). La UI / el usuario escribe este archivo para encender efectos.
// El cerebro lo relee en cada ciclo (barato) para que el cambio sea en vivo.
struct FxControl {
    uint32_t flags = 0;
    float orbe = 0, holos = 0, phi = 0, volterra = 0, supreme = 0;
    float npe = 0, reality = 0, eq = 0;
    uint32_t limiter = 0;
};
FxControl readFxConfig() noexcept {
    FxControl fx;
    std::FILE* f = std::fopen("/data/adb/ivanna_omega/fusion_fx.conf", "r");
    if (!f) return fx;  // sin config = todo apagado (neutro, fail-closed)
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        char name[32]; float v = 0;
        if (std::sscanf(line, "%31[^=]=%f", name, &v) != 2) continue;
        if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;
        if (!std::strcmp(name, "orbe"))     { fx.orbe = v;     if (v > 0) fx.flags |= IVANNA_FX_ORBE; }
        else if (!std::strcmp(name, "holos"))    { fx.holos = v;    if (v > 0) fx.flags |= IVANNA_FX_HOLOS; }
        else if (!std::strcmp(name, "phi"))      { fx.phi = v;      if (v > 0) fx.flags |= IVANNA_FX_PHI; }
        else if (!std::strcmp(name, "volterra")) { fx.volterra = v; if (v > 0) fx.flags |= IVANNA_FX_VOLTERRA; }
        else if (!std::strcmp(name, "supreme"))  { fx.supreme = v;  if (v > 0) fx.flags |= IVANNA_FX_SUPREME; }
        else if (!std::strcmp(name, "npe"))      { fx.npe = v;      if (v > 0) fx.flags |= IVANNA_FX_NPE; }
        else if (!std::strcmp(name, "reality"))  { fx.reality = v;  if (v > 0) fx.flags |= IVANNA_FX_REALITY; }
        else if (!std::strcmp(name, "eq"))       { fx.eq = v;       if (v > 0) fx.flags |= IVANNA_FX_EQ; }
        else if (!std::strcmp(name, "limiter"))  { fx.limiter = (v > 0) ? 1u : 0u; if (v > 0) fx.flags |= IVANNA_FX_LIMITER; }
    }
    std::fclose(f);
    return fx;
}

// Snapshot neutro (defensa en profundidad: aunque makeDefault cambiara,
// esto seguiría siendo neutro).
// Aplica el estado de la fusión + sesión a un snapshot EXISTENTE (lo
// modifica in situ). Solo toca los campos del cerebro; CONSERVA los ajustes
// de efectos de la APK (ORBE, HOLOS, Φ, Volterra, 5 Ejes, NPE, Reality, EQ,
// limitador) que ya trae el snapshot. Así la Ruta B aplica TODOS los efectos
// de la APK en la intercepción, y el cerebro no los borra con un default
// neutro en cada publicación (20 Hz). El default neutro solo se usa en el
// primer publish (makeDefault, fail-closed) si aún no hay snapshot.
void applyFusionToSnapshot(ivanna::OmegaDspSnapshot& s, int32_t sessionId, uint64_t sessionGen,
                           uint64_t generation, uint32_t verdict, double driftPpm, double ratio,
                           int32_t clockSource, double eta, double psiGamma,
                           uint32_t psiActive, uint64_t fusionGen) noexcept {
    s.discovered_session_id = sessionId;
    s.discovered_session_gen = sessionGen;
    s.generation = generation;
    s.timestamp_ms = monotonicMs();
    // ── Fusión (ABI v12): la APK lo lee y se acopla. ─────────────────────
    s.fusion_active = (verdict == IVANNA_VERDICT_LOADED) ? 1u : 0u;
    s.fusion_eta = static_cast<float>(eta);
    s.fusion_psi_gamma = static_cast<float>(psiGamma);
    s.fusion_psi_active = psiActive;
    s.fusion_asrc_verdict = static_cast<int32_t>(verdict);
    s.fusion_asrc_ratio = static_cast<float>(ratio);
    s.fusion_asrc_drift_ppm = static_cast<float>(driftPpm);
    s.fusion_asrc_clock = clockSource;
    s.fusion_generation = fusionGen;
}

} // namespace

int main() {
    ::signal(SIGINT, onSignal);
    ::signal(SIGTERM, onSignal);
    ::signal(SIGPIPE, SIG_IGN);

    ::mkdir("/data/adb/ivanna_omega", 0771);
    ::mkdir("/data/vendor/ivanna_audio", 0771);

    // Kill switch del daemon: si existe, no arrancar.
    if (fileExists("/data/adb/ivanna_omega/ivanna_daemon.off")) return 0;

    // Abrir/crear el estado compartido de la fusión (SHM por archivo + mmap).
    int fd = ::open(IVANNA_FUSION_SHM_PATH, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { logLine("FATAL: no se pudo abrir el estado de fusión"); return 1; }
    ::ftruncate(fd, sizeof(IvannaFusionState));
    auto* fusion = static_cast<IvannaFusionState*>(
        ::mmap(nullptr, sizeof(IvannaFusionState), PROT_READ | PROT_WRITE,
               MAP_SHARED, fd, 0));
    if (fusion == MAP_FAILED) { logLine("FATAL: mmap del estado de fusión falló"); return 1; }
    // Inicializar magic/version si es la primera vez (sequence par = estable).
    {
        IvannaFusionState cur{};
        if (!ivanna_fusion_read(fusion, &cur) || cur.magic != IVANNA_FUSION_MAGIC) {
            IvannaFusionState init{};
            init.magic = IVANNA_FUSION_MAGIC;
            init.version = IVANNA_FUSION_VERSION;
            init.verdict = IVANNA_VERDICT_BYPASS;
            init.ratio = 1.0f;
            ivanna_fusion_write(fusion, &init);
        }
    }

    ivanna::OmegaControlBus bus;
    bool shmOk = false;
    // Snapshot que publica el cerebro: se conserva entre iteraciones para
    // NO borrar los ajustes de efectos de la APK (ORBE, HOLOS, EQ, ...).
    // Cada vuelta se adopta el snapshot más reciente de la app (si publicó
    // algo nuevo) y solo se actualizan los campos del cerebro (fusión +
    // sesión). Así TODOS los efectos de la APK llegan a la Ruta B.
    ivanna::OmegaDspSnapshot lastSnap = ivanna::OmegaDspSnapshot::makeDefault();
    uint64_t snapLastSeenGen = 0;
    uint64_t snapGeneration = 0;
    Kalman3 servo;
    // El núcleo Ψ vivo: el cerebro corre el campo (Ec. 4 η + Ec. 2 ILD +
    // Ec. 3 personalización) y publica sus ganancias por banda en el estado.
    ivanna::psi::PsiCore psi;
    uint64_t psiGeneration = 0;
    // El FIR del campo (lo diseña el cerebro, lo aplica el músculo).
    // Se recalcula solo cuando η o la fuente cambian de forma significativa.
    float psiFirL[ivanna::psi::PsiCore::kPsiFirTaps] = {};
    float psiFirR[ivanna::psi::PsiCore::kPsiFirTaps] = {};
    uint64_t psiFirGen = 0;
    double lastFirEta = -1.0;
    float lastFirR = -1.0f, lastFirTheta = -999.0f;
    bool firDesigned = false;
    int32_t sessionId = 0;
    uint64_t sessionGen = 0;
    uint64_t generation = 0;
    uint64_t servoUpdates = 0;
    int32_t lastPublishedSession = -1;
    // Estado previo del músculo (para calcular deriva instantánea).
    bool havePrev = false;
    int64_t prevMmapNs = 0; uint32_t prevMmapFrames = 0;
    int64_t prevWriteNs = 0; uint32_t prevFramesWritten = 0;
    uint64_t prevTimeMs = 0;
    // Certificación del reloj: diferencia mmap_ns - write_ns (debería ser
    // estable y pequeña si el timestamp mmap es CLOCK_MONOTONIC).
    double clockDiffMean = 0.0; double clockDiffM2 = 0.0; uint64_t clockDiffN = 0;
    bool clockCertified = false;
    uint32_t clockSource = IVANNA_CLOCK_NONE;
    uint32_t verdict = IVANNA_VERDICT_BYPASS;
    int sessionCountdown = 0;  // descubrir sesión cada ~3 s (60 ciclos de 50 ms)

    // Socket de comandos (abstracto @omega_daemon_socket) + hilo que lo
    // sirve (HELLO + GET_STATUS). v2.5.440: antes el socket se bindeaba y
    // nadie lo atendía (el bucle drenaba accept()+close()), así que la app
    // jamás recibía la sesión descubierta y Ruta B no se acoplaba. El hilo
    // lee el estado vivo de cmdState (el bucle lo actualiza cada
    // iteración); si el bind falla, el cerebro sigue sin comandos (la
    // fusión no depende del socket) y la app verá Ruta B sin acople.
    const int sockFd = bindDaemonSocket();
    ivanna::daemon::CommandPlaneState cmdState;
    cmdState.startMs.store(monotonicMs(), std::memory_order_relaxed);
    std::thread cmdThread;
    if (sockFd >= 0) {
        logLine("socket @omega_daemon_socket bindeado");
        cmdThread = std::thread(ivanna::daemon::serveCommandSocket, sockFd,
                                std::ref(cmdState), std::ref(g_running), true);
        logLine("hilo de comandos @omega_daemon_socket iniciado (GET_STATUS/HELLO)");
    } else {
        logLine("WARN: no se pudo bindear @omega_daemon_socket (la fusión sigue; Ruta B sin acople)");
    }

    logLine("ivanna_daemon_v2 (CEREBRO) iniciado — fusión homogénea");

    while (g_running.load()) {
        const uint64_t nowMs = monotonicMs();
        const double dt = havePrev ? (nowMs - prevTimeMs) / 1000.0 : 0.05;
        const bool killSwitch = fileExists("/data/vendor/ivanna_audio/pal_asrc.off");

        // 1. Leer las mediciones del músculo (seqlock; reintentar si cambia).
        IvannaFusionState st{};
        bool readOk = false;
        for (int i = 0; i < 5 && !readOk; ++i) readOk = ivanna_fusion_read(fusion, &st);
        const bool muscleAlive = readOk && st.magic == IVANNA_FUSION_MAGIC
            && st.version == IVANNA_FUSION_VERSION && st.muscle_health == 1;
        const bool muscleFresh = muscleAlive
            && (nowMs - st.muscle_timestamp_ms) < 500;  // el músculo escribe a menudo

        // 2. Deriva instantánea desde el reloj del músculo (si hay datos).
        bool haveMeasurement = false;
        double measPpm = 0.0;
        if (muscleFresh && havePrev && st.stream_rate_hz > 0) {
            const double rate = static_cast<double>(st.stream_rate_hz);
            // Reloj mmap (preferido si está certificado).
            double mmapPpm = 0.0; bool mmapOk = false;
            if (st.clock_mmap_frames != prevMmapFrames && st.clock_mmap_ns != prevMmapNs) {
                double dF = static_cast<double>(st.clock_mmap_frames - prevMmapFrames);
                double dT = static_cast<double>(st.clock_mmap_ns - prevMmapNs) / 1e9;
                if (dT > 0.0 && dF > 0.0) {
                    mmapPpm = (dF / dT / rate - 1.0) * 1e6;
                    mmapOk = std::fabs(mmapPpm) < 5000.0;  // puerta: outlier no
                }
            }
            // Reloj write-timing.
            double writePpm = 0.0; bool writeOk = false;
            if (st.frames_written != prevFramesWritten && st.clock_write_ns != prevWriteNs) {
                double dF = static_cast<double>(st.frames_written - prevFramesWritten);
                double dT = static_cast<double>(st.clock_write_ns - prevWriteNs) / 1e9;
                if (dT > 0.0 && dF > 0.0) {
                    writePpm = (dF / dT / rate - 1.0) * 1e6;
                    writeOk = std::fabs(writePpm) < 5000.0;
                }
            }
            // Certificación del reloj: mmap_ns vs write_ns (misma época).
            if (mmapOk && writeOk) {
                double diffMs = static_cast<double>(st.clock_mmap_ns - st.clock_write_ns) / 1e6;
                ++clockDiffN;
                double delta = diffMs - clockDiffMean;
                clockDiffMean += delta / static_cast<double>(clockDiffN);
                clockDiffM2 += delta * (diffMs - clockDiffMean);
                double var = (clockDiffN > 1) ? clockDiffM2 / (clockDiffN - 1) : 0.0;
                // Certificado si la diferencia es estable (<5 ms de media y poca varianza).
                clockCertified = (std::fabs(clockDiffMean) < 5.0 && var < 25.0 && clockDiffN >= 20);
            }
            // Fusión de sensores: si ambos, promedio; si uno, ese; si ninguno, nada.
            if (mmapOk && writeOk && clockCertified) {
                measPpm = 0.5 * (mmapPpm + writePpm);
                haveMeasurement = true;
                clockSource = IVANNA_CLOCK_MMAP;
            } else if (mmapOk && clockCertified) {
                measPpm = mmapPpm; haveMeasurement = true; clockSource = IVANNA_CLOCK_MMAP;
            } else if (writeOk) {
                measPpm = writePpm; haveMeasurement = true; clockSource = IVANNA_CLOCK_WRITE_TIMING;
            } else if (mmapOk) {
                measPpm = mmapPpm; haveMeasurement = true; clockSource = IVANNA_CLOCK_MMAP;
            } else {
                clockSource = IVANNA_CLOCK_NONE;
            }
        }
        // Guardar el estado previo para el próximo ciclo.
        if (muscleFresh) {
            prevMmapNs = st.clock_mmap_ns; prevMmapFrames = st.clock_mmap_frames;
            prevWriteNs = st.clock_write_ns; prevFramesWritten = st.frames_written;
            prevTimeMs = nowMs; havePrev = true;
        }

        // 3. Servo: predecir + actualizar con la medición (si la hay).
        servo.predict(dt);
        if (haveMeasurement) { servo.update(measPpm); ++servoUpdates; }
        double ratio = servo.ratio();
        double drift = servo.driftPpm();
        // Vigilante: si el ratio diverge demasiado, bypass.
        bool ratioSane = std::fabs(drift) < 1000.0 && ratio > 0.99 && ratio < 1.01;
        if (!ratioSane) { ratio = 1.0; }

        // 4. Descubrir la sesión real cada ~3 s.
        if (--sessionCountdown <= 0) {
            sessionCountdown = 60;
            const auto out = ivanna::daemon::discoverMusicSession();
            if (out.status == ivanna::daemon::SessionDiscoveryStatus::kFound) {
                if (out.sessionId != sessionId) { sessionId = out.sessionId; ++sessionGen; }
            } else if (out.status == ivanna::daemon::SessionDiscoveryStatus::kNone) {
                if (sessionId != 0) { sessionId = 0; ++sessionGen; }
            }
            // kError: se conserva la sesión anterior.
        }

        // 5. Veredicto.
        if (killSwitch) verdict = IVANNA_VERDICT_OFF;
        else if (!muscleAlive || !muscleFresh) verdict = IVANNA_VERDICT_NO_CLOCK;
        else if (!servo.initialized || !haveMeasurement) verdict = IVANNA_VERDICT_BYPASS;
        else if (!ratioSane) verdict = IVANNA_VERDICT_BYPASS;
        else verdict = IVANNA_VERDICT_LOADED;
        if (verdict != IVANNA_VERDICT_LOADED) ratio = 1.0f;

        // 5b. El campo Ψ vivo (el corazón de la fusión).
        // El cerebro actualiza η con la coherencia que mide el músculo
        // (Ec. 4: dη/dt = (η*−η)/τ − λ·L; L = innovación del servo) y
        // calcula las ganancias por banda del campo (Ec. 2 + Ec. 3 + Γ(η)).
        // El músculo las aplica a TODO el audio. El campo solo actúa cuando
        // la fusión está enganchada (LOADED); si no, queda neutro.
        {
            const double coherence = muscleFresh && std::isfinite(st.coherence)
                ? std::clamp(static_cast<double>(st.coherence), 0.0, 1.0) : 0.5;
            const double loss = std::clamp(servo.lastInnovation / 1000.0, 0.0, 1.0);
            psi.update(dt, coherence, loss);
        }
        float psiGainL[ivanna::psi::kPsiBands];
        float psiGainR[ivanna::psi::kPsiBands];
        psi.bandGains(psiGainL, psiGainR);
        const uint32_t psiActive = (verdict == IVANNA_VERDICT_LOADED && !killSwitch) ? 1u : 0u;
        if (psiActive) ++psiGeneration;
        // Diseñar el FIR del campo solo cuando η o la fuente cambian
        // (el campo se mueve en segundos, no en cada muestra).
        if (psiActive) {
            const double dEta = (lastFirEta < 0.0) ? 1.0 : std::fabs(psi.eta - lastFirEta);
            const bool srcChanged = (std::fabs(psi.srcR - lastFirR) > 0.01f)
                || (std::fabs(psi.srcTheta - lastFirTheta) > 0.01f);
            if (!firDesigned || dEta > 0.01 || srcChanged) {
                const float sr = (muscleFresh && st.stream_rate_hz > 0)
                    ? static_cast<float>(st.stream_rate_hz) : 48000.0f;
                psi.designFirs(psiFirL, psiFirR, sr);
                lastFirEta = psi.eta; lastFirR = psi.srcR; lastFirTheta = psi.srcTheta;
                firDesigned = true;
                ++psiFirGen;
            }
        }

        // 6. Control de efectos (config; por defecto todo apagado).
        FxControl fx = readFxConfig();
        if (verdict != IVANNA_VERDICT_LOADED && verdict != IVANNA_VERDICT_BYPASS) {
            fx = FxControl{};  // sin cerebro sano, sin efectos
        }
        if (killSwitch) fx = FxControl{};

        // 7. Escribir las decisiones del cerebro en el estado compartido.
        {
            IvannaFusionState w = st;  // parte de lo último leído (conserva el músculo)
            w.magic = IVANNA_FUSION_MAGIC; w.version = IVANNA_FUSION_VERSION;
            w.generation = ++generation;
            w.session_id = sessionId; w.session_gen = sessionGen;
            w.ratio = static_cast<float>(ratio);
            w.drift_ppm = static_cast<float>(drift);
            w.clock_source = clockSource; w.verdict = verdict;
            w.brain_health = 1; w.ratio_timestamp_ms = nowMs;
            w.servo_updates = servoUpdates;
            w.fx_flags = fx.flags; w.fx_orbe_amount = fx.orbe;
            w.fx_holos_amount = fx.holos; w.fx_phi_amount = fx.phi;
            w.fx_volterra_amount = fx.volterra; w.fx_supreme_amount = fx.supreme;
            w.fx_npe_amount = fx.npe; w.fx_reality_amount = fx.reality;
            w.fx_eq_amount = fx.eq; w.fx_limiter_on = fx.limiter;
            ++w.fx_generation;
            // El campo Ψ vivo (lo calcula el cerebro, lo aplica el músculo).
            w.psi_eta = static_cast<float>(psi.eta);
            w.psi_gamma = static_cast<float>(psi.coupling());
            w.psi_src_r = psi.srcR; w.psi_src_theta = psi.srcTheta; w.psi_src_phi = psi.srcPhi;
            for (int i = 0; i < ivanna::psi::kPsiBands; ++i) {
                w.psi_gain_l[i] = psiGainL[i];
                w.psi_gain_r[i] = psiGainR[i];
            }
            w.psi_active = psiActive;
            w.psi_generation = psiGeneration;
            for (int i = 0; i < ivanna::psi::PsiCore::kPsiFirTaps; ++i) {
                w.psi_fir_l[i] = psiFirL[i];
                w.psi_fir_r[i] = psiFirR[i];
            }
            w.psi_fir_gen = psiFirGen;
            ivanna_fusion_write(fusion, &w);
        }

        // Estado vivo para el hilo de comandos (GET_STATUS): la app lee la
        // sesión descubierta de aquí, no del bucle. Stores atómicos baratos.
        cmdState.sessionId.store(sessionId, std::memory_order_relaxed);
        cmdState.sessionGen.store(sessionGen, std::memory_order_relaxed);
        cmdState.generation.store(generation, std::memory_order_relaxed);
        cmdState.verdict.store(verdict, std::memory_order_relaxed);

        // 8. Publicar el snapshot (SHM del efecto) + heartbeat.
        // READ-MODIFY-WRITE: se adopta el snapshot más reciente de la APK
        // (sus ajustes de efectos: ORBE, HOLOS, Φ, Volterra, 5 Ejes, NPE,
        // Reality, EQ, limitador) y SOLO se actualizan los campos del
        // cerebro (fusión + sesión). El cerebro JAMÁS publica un default
        // neutro que borre los efectos de la APK (antes lo hacía 20 veces
        // por segundo). Así la Ruta B aplica TODOS los efectos de la APK en
        // la intercepción, y el HAL suma ASRC + campo Ψ. La APK lo ve por
        // el snapshot (ABI v12) y la Ruta A cede si fusion_active=1.
        if (!shmOk) shmOk = bus.openWriter();
        if (shmOk) {
            ivanna::OmegaDspSnapshot cur{};
            if (bus.readLatest(cur, snapLastSeenGen)) lastSnap = cur;
            applyFusionToSnapshot(lastSnap, sessionId, sessionGen, ++snapGeneration,
                                  verdict, drift, ratio,
                                  static_cast<int32_t>(st.clock_source),
                                  psi.eta, psi.coupling(), psiActive,
                                  static_cast<uint64_t>(st.sequence));
            if (!bus.publish(lastSnap)) shmOk = false;
            else if (sessionId != lastPublishedSession) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "sesión publicada: %d (gen %llu)",
                              static_cast<int>(sessionId),
                              static_cast<unsigned long long>(sessionGen));
                logLine(buf);
                lastPublishedSession = sessionId;
            }
        }
        writeHeartbeat(sessionId, generation, static_cast<float>(drift), verdict,
                       static_cast<float>(psi.eta), static_cast<float>(psi.coupling()),
                       static_cast<float>(ratio), static_cast<int32_t>(st.clock_source),
                       psiActive);

        // El socket de comandos lo atiende el hilo propio (serveCommandSocket,
        // v2.5.440). Antes este punto drenaba accept()+close() y GET_STATUS
        // jamás se respondía; ese drenaje se eliminó: robaría conexiones al
        // hilo y rompería el acople de Ruta B de forma intermitente.

        ::usleep(50000);  // ~20 Hz
    }

    // El hilo de comandos sale solo cuando g_running cae (select 1 s);
    // se espera antes de cerrar el socket que atiende.
    if (cmdThread.joinable()) cmdThread.join();

    // Al salir, dejar el veredicto en BYPASS y el campo Ψ neutro para que
    // el músculo pase limpio.
    {
        IvannaFusionState cur{};
        if (ivanna_fusion_read(fusion, &cur) && cur.magic == IVANNA_FUSION_MAGIC) {
            IvannaFusionState w = cur;
            w.verdict = IVANNA_VERDICT_BYPASS; w.ratio = 1.0f;
            w.brain_health = 0; w.fx_flags = 0;
            w.psi_active = 0;
            for (int i = 0; i < 32; ++i) { w.psi_gain_l[i] = 1.0f; w.psi_gain_r[i] = 1.0f; }
            ivanna_fusion_write(fusion, &w);
        }
    }
    if (sockFd >= 0) ::close(sockFd);
    logLine("ivanna_daemon_v2 (CEREBRO) detenido (señal)");
    return 0;
}
