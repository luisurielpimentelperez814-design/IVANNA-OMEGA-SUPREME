#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <cmath>
#include <cstring>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <string>
#include <sys/stat.h>

// SAF realtime adaptive state
struct SAFRuntimeState {
    float deltaEnergy = 0.0f;
    float metricNorm = 0.0f;
    float memory = 0.0f;
    float gain = 1.0f;
};




#define SOCKET_PATH "omega_daemon_socket"
#define BUFFER_SIZE 2048

struct DSPParameters {
    float compressorAmount = 0.35f;
    float exciterReduction = 0.15f;
    float highCutHz = 18000.0f;
    float spatialWidth = 1.20f;
    float loudnessTargetLuFS = -14.0f;
    float harmonicGain = 0.85f;
    float antiDolbyIntensity = 0.40f;
    
    // Smoothed parameters
    float currentCompressor = 0.35f;
    float currentSpatialWidth = 1.20f;
    float currentHarmonicGain = 0.85f;
};

static DSPParameters g_dspParams;
static pthread_mutex_t g_paramMutex = PTHREAD_MUTEX_INITIALIZER;

void set_realtime_priority() {
    struct sched_param param;
    param.sched_priority = 80;
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        std::cerr << "[IvannaDaemon] Warning: Could not set SCHED_FIFO priority." << std::endl;
    } else {
        std::cout << "[IvannaDaemon] Realtime SCHED_FIFO priority set successfully." << std::endl;
    }
}

// FIX build APK arm64-v8a (log 2026-07-30, workflow #82935170368):
//   app:buildCMakeDebug[arm64-v8a] FAILED
//   omega_daemon.cpp:49:13: error: cannot use 'try' with exceptions disabled
// El toolchain del NDK compila con -fno-exceptions (regla global del
// proyecto: cero excepciones en C++). std::stof lanza std::invalid_argument
// / std::out_of_range, así que su uso obligaba a un try/catch que aquí no
// puede existir. Sustituido por std::strtof, que reporta el fallo por
// errno + endptr sin lanzar — semántica idéntica (parse best-effort, si
// falla se conserva outVal). Cero cambio de comportamiento observable en
// el daemon; sólo desaparece la excepción.
void parse_json_field(const std::string& json, const std::string& key, float& outVal) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos != std::string::npos) {
        size_t colon = json.find(":", pos);
        if (colon != std::string::npos) {
            const std::string tail = json.substr(colon + 1);
            const char* c_str = tail.c_str();
            char* endptr = nullptr;
            errno = 0;
            const float parsed = std::strtof(c_str, &endptr);
            // Aceptar sólo si strtof consumió ≥1 carácter y no hubo overflow.
            if (endptr != c_str && errno == 0) {
                outVal = parsed;
            }
        }
    }
}

void update_parameters_smooth(const std::string& jsonStr) {
    pthread_mutex_lock(&g_paramMutex);
    parse_json_field(jsonStr, "compressor", g_dspParams.compressorAmount);
    parse_json_field(jsonStr, "exciterReduction", g_dspParams.exciterReduction);
    parse_json_field(jsonStr, "highCutHz", g_dspParams.highCutHz);
    parse_json_field(jsonStr, "spatialWidth", g_dspParams.spatialWidth);
    parse_json_field(jsonStr, "loudnessTargetLuFS", g_dspParams.loudnessTargetLuFS);
    parse_json_field(jsonStr, "harmonicGain", g_dspParams.harmonicGain);
    parse_json_field(jsonStr, "antiDolbyIntensity", g_dspParams.antiDolbyIntensity);

    // Bounds & NaN validation
    if (std::isnan(g_dspParams.compressorAmount)) g_dspParams.compressorAmount = 0.35f;
    if (std::isnan(g_dspParams.spatialWidth)) g_dspParams.spatialWidth = 1.0f;
    if (std::isnan(g_dspParams.harmonicGain)) g_dspParams.harmonicGain = 1.0f;

    g_dspParams.compressorAmount = std::max(0.0f, std::min(1.0f, g_dspParams.compressorAmount));
    g_dspParams.spatialWidth = std::max(0.1f, std::min(3.0f, g_dspParams.spatialWidth));
    g_dspParams.harmonicGain = std::max(0.0f, std::min(2.0f, g_dspParams.harmonicGain));

    pthread_mutex_unlock(&g_paramMutex);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    set_realtime_priority();

    unlink(SOCKET_PATH);
    int serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("[IvannaDaemon] Socket creation failed");
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[IvannaDaemon] Bind failed");
        close(serverFd);
        return 1;
    }

    chmod(SOCKET_PATH, 0666);
    if (listen(serverFd, 10) < 0) {
        perror("[IvannaDaemon] Listen failed");
        close(serverFd);
        return 1;
    }

    std::cout << "[IvannaDaemon] Running Realtime Daemon on " << SOCKET_PATH << std::endl;

    char buffer[BUFFER_SIZE];
    while (true) {
        int clientFd = accept(serverFd, nullptr, nullptr);
        if (clientFd < 0) continue;

        ssize_t bytesRead = read(clientFd, buffer, BUFFER_SIZE - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string req(buffer);
            if (req.find("

// SAF realtime update
// recibe estado desde OmegaEngineBridge
try {
    float safGain = json["gain"];
    float safComp = json["compressor"];
    float safExc = json["exciterReduction"];
    float safSpatial = json["spatialWidth"];

    updateSAFFromJson(
        safGain,
        safComp,
        safExc,
        safSpatial
    );

} catch (...) {}

SET_PERCEPTUAL_STATE") != std::string::npos) {
                update_parameters_smooth(req);
                const char* ack = "{\"status\":\"OK\",\"message\":\"PERCEPTUAL_STATE_APPLIED\"}\n";
                write(clientFd, ack, strlen(ack));
            } else {
                const char* ack = "{\"status\":\"OK\",\"message\":\"COMMAND_PROCESSED\"}\n";
                write(clientFd, ack, strlen(ack));
            }
        }
        close(clientFd);
    }

    close(serverFd);
    unlink(SOCKET_PATH);
    return 0;
}
