#include "../ivanna_hires_config.hpp"
#include "control/command_server.h"
#include "hal_clock_producer.hpp"
#include "../include/audio_hardware_clock_bridge.hpp"
#include "../include/asrc_state_bridge.hpp"
#include "control/peer_auth.hpp"
#include "core/shm_manager.h"
#include "../include/omega_shared.h"
#include "../include/omega_control_bus.h"
#include "../IvannaSelfHealingEngine.hpp"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
#include <cstddef>
#include <cerrno>
#include <csignal>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <android/log.h>
#include <thread>
#include <atomic>
#include <netinet/in.h>
#include <time.h>

#define LOG_TAG "IVANNA_OMEGA_DAEMON"

constexpr const char* OMEGA_SHM_PATH = "/data/adb/ivanna_omega/omega_shm";
constexpr const char* OMEGA_DIR_PATH = "/data/adb/ivanna_omega";
constexpr const char* DEFAULT_LOG_PATH = "/data/adb/ivanna_omega/daemon.log";
constexpr const char* DEFAULT_SOCKET_PATH = "@omega_daemon_socket";

static volatile sig_atomic_t g_running = 1;
static int g_server_fd = -1;
static std::string g_socket_path = DEFAULT_SOCKET_PATH;
static std::string g_log_path = DEFAULT_LOG_PATH;

void log_message(const std::string& msg) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", msg.c_str());
    std::ofstream log_file(g_log_path, std::ios::app);
    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        struct tm tm_buf{};
        localtime_r(&now, &tm_buf);
        log_file << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
                 << " [IVANNA-DAEMON] " << msg << std::endl;
    }
}

// Threads created by an SCHED_FIFO creator can inherit real-time scheduling.
// Control, polling and watchdog workers must drop to best-effort before doing
// I/O. Keep deadline-critical audio processing out of this helper.
static void setCurrentThreadBestEffort(const char* role) noexcept {
    struct sched_param p{};
    errno = 0;
    const int rc = sched_setscheduler(0, SCHED_OTHER, &p);
    const int err = (rc == 0) ? 0 : errno;
    const int policy = sched_getscheduler(0);
    if (rc != 0 || policy != SCHED_OTHER) {
        // Avoid std::string/ofstream from this noexcept thread-entry helper.
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "SCHED-WORKER: %s could not confirm SCHED_OTHER rc=%d errno=%d policy=%d",
                            role, rc, err, policy);
    }
}

static std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_shutdown_requested.store(true, std::memory_order_release);
        // Signal handler seguro: no hacer IO, malloc, mutex ni logging aquí.
        // El loop principal detecta g_running y realiza el cierre ordenado.
        g_running = 0;
    }
}

void ensure_directory_exists(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0 && errno != EEXIST) {
            log_message("Warning: Failed to create directory " + std::string(path) + ": " + strerror(errno));
        }
    }
}

int setup_shared_memory(int sampleRate) {
    ensure_directory_exists(OMEGA_DIR_PATH);

    // omega_shm recovery: eliminar estado incompleto de una ejecución anterior
    try {
        if (std::filesystem::exists("/data/adb/ivanna_omega/omega_shm")) {
            // El gestor SHM valida contenido posteriormente.
            // No se elimina automáticamente para preservar estado válido.
        }
    } catch (...) {
        std::cerr << "[IVANNA-DAEMON] omega_shm recovery check failed" << std::endl;
    }

    log_message("Initializing Shared Memory via OmegaShmManager at: " + std::string(OMEGA_SHM_PATH));

    if (!ivanna::shmManager().init(OMEGA_SHM_PATH)) {
        log_message("Error: OmegaShmManager::init() fallo");
        return -1;
    }

    void* base = ivanna::shmManager().base();

    if (base != nullptr) {
        // Estado en offset fijo SHM_STATE_OFFSET (pagina 1): desacoplado de
        // sizeof(ShmHeader) y jamas solapa la pagina de control (seqlock+frames).
        auto* hdr = static_cast<ivanna::ShmHeader*>(base);
        auto* state =
            reinterpret_cast<OmegaSharedState*>(
                static_cast<uint8_t*>(base)
                + ivanna::SHM_STATE_OFFSET
            );

        const bool layout_ok =
            hdr->magic      == ivanna::OMEGA_SHM_MAGIC &&
            hdr->version    == ivanna::OMEGA_SHM_VERSION &&
            hdr->state_size == static_cast<uint32_t>(sizeof(OmegaSharedState)) &&
            state->state_magic.load(std::memory_order_acquire) == OMEGA_STATE_MAGIC;

        if (layout_ok) {
            // Daemon reiniciado con la app viva: reattach SIN placement-new
            // destructivo sobre el estado compartido.
            log_message("OmegaSharedState: magic OK - reattach sin reinicializar");
            // FIX (2026-09-17): en reattach NO se toca is_processing ni
            // current_latency_ms — la app esta leyendo esos campos AHORA
            // MISMO y pisarlos con valores de arranque (true / 0.0) borra
            // el estado real que el daemon anterior habia publicado. Solo
            // el primer arranque (layout incompatible) inicializa.
        } else {
            log_message("OmegaSharedState: init (primer arranque o layout incompatible)");
            new(state) OmegaSharedState();
            state->state_magic.store(OMEGA_STATE_MAGIC, std::memory_order_release);
            state->is_processing.store(true);
            state->current_latency_ms.store(0.0f);
        }

        log_message("OmegaSharedState listo (offset=4096, sizeof=" +
                    std::to_string(sizeof(OmegaSharedState)) + ", SHM_SIZE=" +
                    std::to_string(ivanna::SHM_SIZE) + ")");
    }

    log_message("Shared Memory listo fd=" + std::to_string(ivanna::shmManager().fd()));

    // Deteccion de crash: si el apagado anterior no fue limpio, registrarlo
    if (!ivanna::shmManager().previousShutdownClean()) {
        ivanna::shmManager().noteCrashDetected();
        log_message("AVISO: apagado anterior NO limpio (crash/kill) — crash_count incrementado");
    }
    ivanna::shmManager().markShutdownClean(false); // se pondra a 1 solo en apagado limpio

    return ivanna::shmManager().fd();
}

int create_socket_server(const std::string& socket_path) {
    if (socket_path[0] != '@') unlink(socket_path.c_str());
    int server_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) return -1;
    int one = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_un addr; memset(&addr,0,sizeof(addr));
    addr.sun_family = AF_UNIX;
    socklen_t addr_len = 0;
    if (socket_path[0] == '@') {
        std::string name = socket_path.substr(1);
        addr.sun_path[0] = '\0';
        memcpy(addr.sun_path+1, name.c_str(), name.size());
        addr_len = offsetof(struct sockaddr_un, sun_path) + 1 + name.size();
    } else {
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path)-1);
        addr_len = sizeof(addr);
    }
    if (bind(server_fd, (struct sockaddr*)&addr, addr_len) < 0) {
        int e = errno;
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "bind(%s) errno=%d (%s)", socket_path.c_str(), e, strerror(e));
        close(server_fd); return -1;
    }
    if (socket_path[0] != '@' && chmod(socket_path.c_str(), 0600) != 0) {
        int e = errno;
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "chmod(0600) %s errno=%d (%s)", socket_path.c_str(), e, strerror(e));
        close(server_fd); unlink(socket_path.c_str()); return -1;
    }
    if (listen(server_fd, 16) < 0) {
        int e = errno;
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "listen(%s) errno=%d (%s)", socket_path.c_str(), e, strerror(e));
        close(server_fd); return -1;
    }
    log_message("Socket server active: " + socket_path);
    return server_fd;
}

static bool validate_shm_region(void* ptr, size_t size) {
    if (!ptr) {
        fprintf(stderr, "[IVANNA] omega_shm mmap returned nullptr\n");
        return false;
    }
    if (size < 4096) {
        fprintf(stderr, "[IVANNA] omega_shm invalid size=%zu\n", size);
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    // FIX (corrupción real, commit 37b30e12): este try{} faltaba por completo
    // — el catch de más abajo (fatal exception / fatal unknown exception) se
    // había agregado SIN su try correspondiente, error de sintaxis genuino
    // ("expected expression" / "expected '}'" en CI). Se envuelve todo el
    // cuerpo de main() para que ambos catch atrapen cualquier excepción no
    // manejada del daemon completo, tal como indica su intención original.
    try {
    std::string socket_path = DEFAULT_SOCKET_PATH;
    int rate = 48000, buffer = 64; bool realtime = false;
    ensure_directory_exists(OMEGA_DIR_PATH);
    int tcp_port = 0; bool hal_clock_enabled = true;
    for (int i=1;i<argc;i++) {
        std::string arg=argv[i];
        if (arg=="--socket" && i+1<argc) socket_path=argv[++i];
        else if (arg=="--rate" && i+1<argc) { try{rate=std::stoi(argv[++i]);}catch(...){rate=48000;} }
        else if (arg=="--buffer" && i+1<argc) { try{buffer=std::stoi(argv[++i]);}catch(...){buffer=64;} }
        else if (arg=="--realtime") realtime=true;
        else if (arg=="--tcp-port" && i+1<argc) { try{ tcp_port = std::stoi(argv[++i]); } catch(...) { tcp_port = 0; } }
        else if (arg=="--hal-clock") hal_clock_enabled=true;
        else if (arg=="--no-hal-clock") hal_clock_enabled=false;
    }
    g_socket_path=socket_path;
    (void)buffer;
    { int hr=0, hd=0; if (ivanna::hires::readConf("/data/adb/ivanna_omega/hires.conf", hr, hd)) rate = hr; }
    struct sigaction sa; memset(&sa,0,sizeof(sa)); sa.sa_handler=signal_handler;
    sigaction(SIGINT,&sa,nullptr); sigaction(SIGTERM,&sa,nullptr);
    signal(SIGPIPE, SIG_IGN);
    log_message("IVANNA-OMEGA Daemon iniciado; control plane con SCHED_OTHER.");
    // polling), not the PCM callback. Giving this main thread SCHED_FIFO—even
    // at 80—risks propagating RT scheduling to workers and can harm touch/system
    // responsiveness while it performs I/O. Keep it SCHED_OTHER; only a measured,
    // bounded audio callback may request high RT priority in its own thread.
    // --realtime remains accepted for service.sh compatibility, but is not a
    // license to elevate this control loop.
    struct sched_param control_sched{};
    errno = 0;
    const int control_sched_rc = sched_setscheduler(0, SCHED_OTHER, &control_sched);
    const int control_sched_errno = (control_sched_rc == 0) ? 0 : errno;
    const int control_policy = sched_getscheduler(0);
    if (control_sched_rc == 0 && control_policy == SCHED_OTHER) {
        log_message(std::string("SCHED-CONTROL: main thread pinned to SCHED_OTHER; --realtime=") +
                    (realtime ? "requested but intentionally not applied to control plane" : "not requested"));
    } else {
        log_message("SCHED-CONTROL: failed to confirm SCHED_OTHER rc=" +
                    std::to_string(control_sched_rc) + " errno=" +
                    std::to_string(control_sched_errno) + " policy=" +
                    std::to_string(control_policy));
    }
    // FIX (resiliencia): la SHM es opcional para el socket de comandos. Si
    // /data/adb/ivanna_omega no está montado/escribible aún, shmManager.init
    // falla y (antes) el socket nacía paralizado. El daemon DEBE bindear el
    // socket aunque la SHM falle — el panel usa el socket para telemetría.
    if (setup_shared_memory(rate) < 0) {
        log_message("WARN: SHM no disponible — el daemon sigue sirviendo el socket de comandos (modo degradado).");
    }
    log_message("DSP sample rate configured: " + std::to_string(rate));
    // FIX (socket DESCONECTADO con daemon CORRIENDO, 2026-08-31): si bind()
    // falla — EADDRINUSE por una instancia zombie que aun retiene el nombre
    // abstracto (no se puede unlink en el abstract namespace), o early-boot
    // con el namespace no listo — el daemon moria en silencio y service.sh lo
    // relanzaba en bucle: panel muestra "CORRIENDO" pero el socket jamas
    // queda bindeado (ausente en /proc/net/unix). Reintentar con backoff y
    // loguear errno al log persistente en cada intento.
    {
        int attempt = 0;
        while (g_running && (g_server_fd = create_socket_server(socket_path)) < 0) {
            int e = errno;
            log_message("bind(" + socket_path + ") fallo: errno=" + std::to_string(e) +
                        " (" + strerror(e) + ") — reintento " + std::to_string(attempt + 1) + "/12 en 500 ms");
            usleep(500000);
            if (++attempt >= 12) {
                log_message("bind agotado — el daemon aborta para que service.sh lo relance con backlog limpio");
                return 1;
            }
        }
    }
    if (g_server_fd < 0) return 1;

    CommandServer commandServer;
    commandServer.resetState();
    if (commandServer.start("@omega_command_socket")) {
        log_message("CONTROL socket ready: @omega_command_socket");
        std::thread([&commandServer](){
            setCurrentThreadBestEffort("command-server");
            commandServer.acceptLoop();
        }).detach();
    } else {
        // FIX: antes este fallo era SILENCIOSO. El socket de control es
        // secundario — no abortamos el daemon principal, solo lo logueamos.
        log_message("WARN: no se pudo bindear @omega_command_socket (secundario) — el daemon principal sigue activo.");
    }

    // Watchdog de heartbeat DEDICADO: 1 Hz, no depende del accept loop ni de @omega_command_socket.
    // Si el accept loop se bloquea (cliente colgado), el heartbeat sigue
    // latiendo y la app distingue "daemon vivo, socket ocupado" de "daemon muerto".
    std::thread([](){
        setCurrentThreadBestEffort("heartbeat-watchdog");
        const auto t0 = std::chrono::steady_clock::now();
        uint64_t beats = 0;
        auto publishNowMs = []() noexcept {
            struct timespec ts{};
            clock_gettime(CLOCK_MONOTONIC, &ts);
            const uint64_t monoMs = static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
                                    static_cast<uint64_t>(ts.tv_nsec / 1000000ULL);
            ivanna::shmManager().publishHeartbeatMs(monoMs);
        };
        publishNowMs();
        while (!g_shutdown_requested.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            ++beats;
            publishNowMs();
            ivanna::shmManager().bumpHealthCounter(1);   // heartbeats_emitidos
            ivanna::shmManager().publishUptime(
                (uint64_t)std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - t0).count());
            // RSS desde /proc/self/status (VmRSS), sin dependencias externas
            if ((beats & 7) == 0) {  // cada 8 s basta
                std::ifstream f("/proc/self/status"); std::string line;
                while (std::getline(f, line)) {
                    if (line.rfind("VmRSS:", 0) == 0) {
                        ivanna::shmManager().publishRssKb((uint32_t)std::atol(line.c_str() + 6));
                        break;
                    }
                }
            }
        }
    }).detach(); // heartbeat_watchdog

    Ivanna::IvannaSelfHealingEngine selfHealer;
    selfHealer.startMonitoring();
    // ── PRODUCTOR DEL RELOJ HAL (el eslabon que faltaba) ────────────────────
    // El efecto lee el snapshot y el panel "Reloj hardware" lo muestra, pero en la
    // ruta Magisk nadie lo publicaba. Este hilo lo publica SOLO con pares
    // (frames, timestamp) correlacionados leidos del driver en una unica consulta.
    // Fail-closed: sin fuente fiable no publica nada y lo declara.
    std::thread hal_clock_thread;
    if (hal_clock_enabled) {
        hal_clock_thread = std::thread([hal_clock_enabled](){
            setCurrentThreadBestEffort("hal-clock-asrc-poller");
            ivanna::audio::halclock::HalClockProducer::Config c;
            ivanna::audio::halclock::HalClockProducer producer(c);
            producer.setEnabled(true);
            auto& st = producer.status();
            (void)st;
            ivanna::audio::halclock::ProducerState last =
                ivanna::audio::halclock::ProducerState::kDisabled;
            std::string lastReason;

            // ── CORE ASRC SOFTWARE (el eslabon que faltaba, parte 2) ───────────
            // AsrcClockController compilaba y tenia tests propios desde antes,
            // pero NADIE lo instanciaba: cero consumidores reales fuera de su
            // propio test. Aqui se alimenta con la MISMA muestra que el productor
            // de reloj acaba de validar y publicar (se relee via HardwareClockReader,
            // nunca se inventa un par frames+timestamp propio), y su decision
            // (TRACKING/HOLD/BYPASS/FAULT + correccion en ppm) se publica en su
            // propio canal para que la app la muestre sin ADB. Sigue siendo un
            // servo SOFTWARE sobre el resampler de la app: no identifica ni
            // acciona ningun ASRC fisico del Moto G85.
            ivanna::dsp::AsrcClockController asrcController;
            ivanna::audio::AsrcStatePublisher asrcPublisher;
            ivanna::audio::HardwareClockReader clockReader;
            ivanna::dsp::AsrcState lastAsrcState = ivanna::dsp::AsrcState::INIT;
            std::string lastAsrcReason;

            for (;;) {
                const auto s = producer.poll();
                if (s.state != last || std::string(s.reason) != lastReason) {
                    log_message(std::string("HAL-CLOCK ") +
                        ivanna::audio::halclock::stateName(s.state) +
                        " dev=" + (s.device.empty() ? std::string("-") : s.device) +
                        " motivo=" + s.reason +
                        " pub=" + std::to_string(s.published) +
                        " rech=" + std::to_string(s.rejected) +
                        (s.measuredRateHz > 0.0
                             ? " rate=" + std::to_string(s.measuredRateHz) +
                               " deriva_ppm=" + std::to_string(s.driftPpm)
                             : std::string()));
                    last = s.state; lastReason = s.reason;
                }

                // The daemon must consume a snapshot published by the actual
                // HAL/PAL stream owner. It must never gate reading on its own PCM
                // producer: that producer intentionally stays blocked unless an
                // owner-bound source is injected. A leftover snapshot from an old
                // route/process is rejected unless its monotonic timestamp is fresh.
                ivanna::dsp::ClockSample cs{};
                bool haveSample = false;
                if (clockReader.open()) {
                    ivanna::audio::HardwareClockSample hcs{};
                    if (clockReader.read(hcs)) {
                        timespec now{};
                        if (::clock_gettime(CLOCK_MONOTONIC, &now) == 0 &&
                            now.tv_sec >= 0 && now.tv_nsec >= 0 &&
                            now.tv_nsec < 1000000000L) {
                            const uint64_t nowNs =
                                static_cast<uint64_t>(now.tv_sec) * 1000000000ull +
                                static_cast<uint64_t>(now.tv_nsec);
                            if (ivanna::audio::isFreshMonotonicHardwareClockSample(hcs, nowNs)) {
                                cs.presentedFrames = hcs.presentedFrames;
                                cs.timestampNs = static_cast<int64_t>(hcs.hardwareTimestampNs);
                                cs.nominalRateHz = hcs.nominalSampleRateHz;
                                haveSample = true;
                            }
                        }
                    }
                }
                const bool routeValidated = haveSample;
                if (!routeValidated) asrcController.reset();
                const auto asrcSnap = asrcController.update(
                    cs, routeValidated, hal_clock_enabled);
                if (asrcSnap.state != lastAsrcState || std::string(asrcSnap.reason) != lastAsrcReason) {
                    log_message(std::string("ASRC-SW ") +
                        ivanna::audio::asrcStateName(asrcSnap.state) +
                        " motivo=" + asrcSnap.reason +
                        " deriva_ppm=" + std::to_string(asrcSnap.driftPpm) +
                        " correccion_ppm=" + std::to_string(asrcSnap.targetPpm) +
                        " ratio=" + std::to_string(asrcSnap.appliedRatio));
                    lastAsrcState = asrcSnap.state; lastAsrcReason = asrcSnap.reason;
                }
                if (!asrcPublisher.open()) {
                    log_message("ASRC-SW no se pudo abrir el canal de publicacion de estado");
                } else {
                    asrcPublisher.publish(asrcSnap);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(250));
            }
        });
        hal_clock_thread.detach();
        log_message("HAL-CLOCK daemon activo en fail-closed: sin callsite HAL/PAL propietario no abre PCM ni publica reloj");
        log_message("ASRC-SW core activo: solo consume snapshots HAL/PAL externos, frescos y coherentes; sin ellos permanece bloqueado");
    } else {
        log_message("HAL-CLOCK productor deshabilitado por --no-hal-clock");
        log_message("ASRC-SW core deshabilitado (depende de --hal-clock)");
    }

    log_message("Self-Healing Engine activated and monitoring components.");

    if (!ivanna::controlBus().openWriter()) {
        // FIX: degradado, no fatal. El socket de comandos sigue sirviendo
        // STATUS/TELEMETRY aunque el bus DSP no abra (p.ej. sin consumidor
        // omega_effect aún). Antes este fallo dejaba el socket funcional
        // pero el panel interpretaba silencio como muerte.
        log_message("WARN: OmegaControlBus writer no abrió — modo degradado, socket de comandos sigue sirviendo.");
    } else {
        log_message("Master Control Bus: OmegaControlBus writer opened successfully.");
    }

    // El listener TCP de loopback no ofrece SO_PEERCRED ni token de autenticación.
    // Se deshabilita incluso si se pasa --tcp-port; el control soportado usa UNIX
    // sockets con validación de credenciales de kernel.
    if (tcp_port > 0) {
        log_message("TCP fallback rechazado por seguridad: control solo por UNIX socket autenticado.");
    }

    // FIX (daemon bloqueante / fd leak / buffer overflow):
    // 1. El accept loop anterior atendía clientes en el hilo principal de forma
    //    secuencial bloqueante — mientras un cliente tenía la conexión abierta
    //    (OmegaDaemon.kt mantiene su socket abierto de forma persistente), ningún otro podía
    //    conectarse. Se despacha cada cliente en un thread propio (detached).
    // 2. json_buf[8192] stack con recv parcial causaba lecturas fragmentadas
    //    sin NUL correcto. Se aumenta a 65536 y se acumula en heap.
    // 3. reply declarado dos veces en el mismo scope (UB/shadow) — unificado.
    while (g_running) {
        // FIX (2026-09-17): el heartbeat ya tiene su propio thread dedicado
        // (heartbeat_watchdog, 1 Hz, lineas ~255-276) que escribe
        // SHM_HEARTBEAT_OFFSET y bumpHealthCounter(1). El heartbeat que
        // este loop escribia aqui era REDUNDANTE — mismo offset, mismo
        // contador, doble escritura por segundo. El loop principal ahora
        // solo hace: self-healing pings + accept de clientes. El select()
        // con timeout de 1s sigue siendo el mecanismo de wakeup (necesario
        // para detectar g_running=0 sin busy-wait).
        selfHealer.pingAudioEngine();
        selfHealer.pingIpcSocket();
        selfHealer.pingDspKernel();
        
        Ivanna::DiagnosticReport report = selfHealer.getDiagnosticReport();
        if (report.restartCount > 0) {
            log_message("Self-Healing intervention! Total recoveries: " + std::to_string(report.restartCount));
        }
        // FIX (flanco integración cruzada): antes solo se logueaba local —
        // nunca llegaba a ningún cliente conectado (la app). Se reporta a
        // AMBAS instancias de CommandServer (no está claro cuál sirve el
        // socket real que consume la app hoy, así que se actualizan las dos
        // por seguridad — costo nulo, un pthread_mutex_lock/unlock más por
        // segundo).
        commandServer.reportSelfHealRestarts((uint32_t)report.restartCount);

        fd_set readfds; FD_ZERO(&readfds); FD_SET(g_server_fd,&readfds);
        struct timeval tv{1,0};
        int activity = select(g_server_fd+1,&readfds,NULL,NULL,&tv);
        if (activity<0 && errno!=EINTR) break;
        if (activity>0 && FD_ISSET(g_server_fd,&readfds)) {
            struct sockaddr_un client_addr; socklen_t client_len=sizeof(client_addr);
            int client_fd = accept(g_server_fd,(struct sockaddr*)&client_addr,&client_len);
            if (client_fd<0) continue;
            if (!ivanna::security::isAuthorizedControlPeer(client_fd)) {
                if (ivanna::security::shouldLogRejectedPeer()) {
                    log_message("Conexión rechazada en socket daemon: UID no autorizado (registro limitado)");
                }
                close(client_fd);
                continue;
            }
            ivanna::shmManager().bumpHealthCounter(4); // clientes_conectados
            commandServer.bumpClientsServed(); // telemetria visible en GET_STATUS

            // FIX (concurrencia): antes el bucle principal atendia a UN cliente
            // en un recv-loop bloqueante. El bridge Kotlin mantiene su socket
            // abierto de forma persistente, asi que mientras ese cliente
            // mandara trafico el daemon jamas volvia a accept() y cualquier
            // otra conexion (reinstanciacion del engine, hot-reload) se
            // pudria en el backlog. Ahora cada conexion va a su propio hilo
            // (los handlers de CommandServer ya estan protegidos por mutex).
            std::thread([client_fd, &commandServer]() {
                setCurrentThreadBestEffort("control-client");
                struct timeval rcv_tv{0,150000}; setsockopt(client_fd,SOL_SOCKET,SO_RCVTIMEO,&rcv_tv,sizeof(rcv_tv));
                struct timeval snd_tv{0,300000}; setsockopt(client_fd,SOL_SOCKET,SO_SNDTIMEO,&snd_tv,sizeof(snd_tv));

                // FIX (framing): un socket stream NO garantiza mensajes
                // completos por recv() — un JSON grande podia llegar partido
                // y parsearse roto (comando perdido en silencio). Se acumula
                // y se extraen objetos JSON completos por balance de llaves
                // (respetando comillas y escapes); el resto queda en buffer
                // para el siguiente recv.
                std::string pending;
                pending.reserve(16384);
                char chunk[8192];
                bool first_read = true;
                while (g_running) {
                    ssize_t nbytes = recv(client_fd, chunk, sizeof(chunk), 0);
                    if (nbytes<=0) {
                        // FIX (handshake SHM real — "Modo B" detectaba el
                        // timeout pero nunca enviaba el fd, solo colgaba):
                        // ShmManager.kt (mapFromDaemon) conecta y a propósito
                        // NO escribe nada durante el timeout de 150ms de
                        // arriba, precisamente para que el daemon lo
                        // distinga de un cliente de comandos de texto/JSON.
                        // Antes ambos casos (timeout silencioso y EOF real)
                        // caían al mismo break sin diferenciarse — el fd de
                        // ivanna::shmManager().fd() nunca salía del daemon
                        // aunque la SHM ya estuviera lista. Se distingue
                        // timeout real (EAGAIN/EWOULDBLOCK, nbytes<0) de EOF
                        // (nbytes==0, el cliente cerró la conexión): solo el
                        // timeout en el PRIMER recv (nada recibido aún)
                        // dispara el envío del fd por SCM_RIGHTS.
                        if (first_read && nbytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            int shm_fd = ivanna::shmManager().fd();
                            if (shm_fd >= 0) {
                                char one = 0;
                                struct iovec io;
                                io.iov_base = &one;
                                io.iov_len  = 1;
                                char cmsgbuf[CMSG_SPACE(sizeof(int))];
                                std::memset(cmsgbuf, 0, sizeof(cmsgbuf));
                                struct msghdr msg;
                                std::memset(&msg, 0, sizeof(msg));
                                msg.msg_iov        = &io;
                                msg.msg_iovlen      = 1;
                                msg.msg_control     = cmsgbuf;
                                msg.msg_controllen  = sizeof(cmsgbuf);
                                struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
                                cmsg->cmsg_level = SOL_SOCKET;
                                cmsg->cmsg_type  = SCM_RIGHTS;
                                cmsg->cmsg_len   = CMSG_LEN(sizeof(int));
                                std::memcpy(CMSG_DATA(cmsg), &shm_fd, sizeof(int));
                                const ssize_t sent = sendmsg(client_fd, &msg, MSG_NOSIGNAL);
                                if (sent < 0) {
                                    const int send_errno = errno;
                                    log_message(std::string("Modo B: sendmsg(SCM_RIGHTS) fallo errno=") +
                                                std::to_string(send_errno) + " (" + strerror(send_errno) + ")");
                                } else if (sent != 1) {
                                    log_message("Modo B: sendmsg(SCM_RIGHTS) entrega incompleta: bytes=" +
                                                std::to_string(sent) + " esperado=1");
                                } else {
                                    log_message("Modo B: fd de omega_shm entregado por SCM_RIGHTS (1 byte + 1 fd)");
                                }
                            } else {
                                log_message("Modo B: cliente silencioso pero shmManager().fd() invalido (SHM aun no inicializada)");
                            }
                        }
                        break;
                    }
                    first_read = false;
                    pending.append(chunk, (size_t)nbytes);

                    // Descarta whitespace inter-mensaje.
                    size_t start = pending.find_first_not_of(" \t\r\n");
                    if (start==std::string::npos) { pending.clear(); continue; }
                    pending.erase(0, start);

                    if (pending[0]!='{') {
                        // Comandos de texto plano (ruta MagiskBridge): se
                        // procesa la linea completa hasta '\n' si existe, o el
                        // buffer entero (comportamiento historico).
                        std::string text = pending;
                        size_t nl = pending.find('\n');
                        if (nl!=std::string::npos) { text = pending.substr(0, nl); pending.erase(0, nl+1); }
                        else pending.clear();
                        char reply[1024]={};
                        int rlen = commandServer.handleTextCommand(text.c_str(), reply, sizeof(reply));
                        if (rlen>0) send(client_fd, reply, rlen, MSG_NOSIGNAL);
                        continue;
                    }

                    // Extrae el primer objeto JSON completo del buffer.
                    int depth=0; bool inStr=false, esc=false; size_t end=std::string::npos;
                    for (size_t i=0;i<pending.size();++i) {
                        char c=pending[i];
                        if (inStr) { if (esc) esc=false; else if (c=='\\') esc=true; else if (c== '"') inStr=false; continue; }
                        if (c=='"') inStr=true;
                        else if (c=='{') ++depth;
                        else if (c=='}' && --depth==0) { end=i+1; break; }
                    }
                    if (end==std::string::npos) {
                        // JSON incompleto: espera al siguiente fragmento.
                        if (pending.size() > 65536) pending.clear(); // proteccion anti-flood
                        continue;
                    }
                    std::string js = pending.substr(0, end);
                    pending.erase(0, end);

                    std::string action="UNKNOWN";
                    auto pos=js.find("\"action\"");
                    if (pos!=std::string::npos) {
                        auto p1=js.find("\"",pos+8); auto p2=js.find("\"",p1+1);
                        if (p1!=std::string::npos && p2!=std::string::npos) action=js.substr(p1+1,p2-p1-1);
                    }
                    char reply[4096]={};
                    int rlen = commandServer.handleJsonCommand(js.c_str(), reply, sizeof(reply));
                    if (rlen>0) {
                        send(client_fd, reply, rlen, MSG_NOSIGNAL);
                    } else {
                        std::string err = "{\"status\":\"error\",\"reason\":\"not_implemented\",\"action\":\"" + action + "\"}";
                        send(client_fd, err.c_str(), err.size(), MSG_NOSIGNAL);
                    }
                }
                close(client_fd);
            }).detach();
        }

    }
    if (g_server_fd>=0) close(g_server_fd);
    commandServer.stop();
    selfHealer.stopMonitoring();
    ivanna::shmManager().markShutdownClean(true);
    log_message("Daemon shutdown REAL");
    }
    catch (const std::exception& e) {
        std::cerr << "[IVANNA-DAEMON] fatal exception: " << e.what() << std::endl;
        return 1;
    }
    catch (...) {
        std::cerr << "[IVANNA-DAEMON] fatal unknown exception" << std::endl;
        return 1;
    }

    // Apagado limpio: marcar ANTES de cerrar el SHM — el proximo arranque
    // distingue cierre ordenado de crash/kill -9.
    ivanna::shmManager().markShutdownClean(true);
    log_message("Daemon apagado limpio (shutdown_clean=1)");
    return 0;
}
