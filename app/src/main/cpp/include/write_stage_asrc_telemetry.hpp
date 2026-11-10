#pragma once
// write_stage_asrc_telemetry.hpp — relevo de SOLO LECTURA del dump del
// write-stage ASRC PAL (ivanna_asrc_write_stage.c, v5) al daemon/app.
//
// QUÉ ES: la .so del write-stage (cargada por LD_PRELOAD dentro del HAL de
// audio, /vendor/bin/hw/android.hardware.audio.service_64) reescribe cada
// 5 s /data/vendor/ivanna_audio/pal_asrc_stage.txt con ratio, drift_ppm,
// kalman_drift, sat, opens/ok, clock=, fir_taps y neon. El dump es 0644 y
// vive en /data/vendor/ivanna_audio, un directorio que la app
// (untrusted_app) no puede abrir; el daemon (root) sí. Este header da al
// daemon un parser fail-closed de esa primera línea para relevarla en
// GET_CLOCK_TELEMETRY, el mismo canal que ya releva el estado ASRC
// software (asrc_state_bridge.hpp).
//
// QUÉ NO ES: no toca el ASRC, no escribe el dump, no cambia ningún ratio
// ni abre el HAL. Si el archivo no existe, está viejo (>30 s, el dump se
// reescribe cada 5 s), no trae la cabecera "v5" o trae valores fuera de
// rango, el resultado es valid=false y el llamador debe reportar
// "no disponible", nunca simular un valor.
//
// Formato esperado (primera línea, dump_once() en ivanna_asrc_write_stage.c):
//   IVANNA ASRC WRITE STAGE v5 pid=%d enabled=%d ratio=%.9f drift_ppm=%.3f
//   clock=%s mmap_ok=%llu ... opens=%llu ok=%llu ... sat=%d ... kalman_drift=%.3f
//   ... fir_taps=%d dither=%d neon=%d up192=%d
// Solo se parsean los campos que el panel necesita; el resto del dump
// (líneas STREAM por stream) queda para inspección manual en el teléfono.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace ivanna::audio {

inline constexpr const char* kWriteStageAsrcDumpPath =
        "/data/vendor/ivanna_audio/pal_asrc_stage.txt";
inline constexpr const char* kWriteStageAsrcDumpHeader =
        "IVANNA ASRC WRITE STAGE v5";
// El dump se reescribe cada 5 s; más de 30 s sin reescribir = HAL sin el
// write-stage, dump viejo de otro proceso, o teléfono sin música.
inline constexpr uint64_t kWriteStageAsrcMaxAgeMs = 30000;
// El servo v5 tiene clamp ±1000 ppm (RATIO_MIN/MAX en
// ivanna_asrc_write_stage.c); la cota de cordura del servo es ±5000 ppm.
inline constexpr double kWriteStageAsrcMaxDriftPpm = 5000.0;
inline constexpr int kWriteStageAsrcFirTapsV5 = 287;

struct WriteStageAsrcSample {
    bool valid = false;
    int pid = 0;                // PID del HAL que escribe el dump
    bool enabled = false;       // kill-switch pal_asrc.off ausente
    double ratio = 1.0;         // ratio aplicado por el resampler
    double driftPpm = 0.0;      // deriva del servo (ppm)
    double kalmanDriftPpm = 0.0;// deriva estimada por el Kalman (ppm)
    char clock[16]{};           // "mmap" | "write-timing" | "unknown"
    uint64_t opens = 0;         // streams PAL abiertos desde el arranque
    uint64_t ok = 0;            // opens con interposición correcta
    int sat = 0;                // 1 = servo saturado en el clamp
    int firTaps = 0;            // 287 en v5
    int neon = 0;               // 1 = FIR NEON activo
    uint64_t ageMs = 0;         // antigüedad del dump al leerlo
};

namespace write_stage_detail {

// Busca `key` como token completo: al inicio de línea o precedido por un
// espacio. Sin esto, "ok=" casaría dentro de "mmap_ok=" y "sat=" dentro
// de "sat_ticks=" no sería problema pero "ok=" sí lo es.
inline const char* findToken(const char* line, const char* key) noexcept {
    const size_t keyLen = std::strlen(key);
    const char* p = line;
    while ((p = std::strstr(p, key)) != nullptr) {
        if (p == line || p[-1] == ' ' || p[-1] == '\t') return p;
        p += keyLen;
    }
    return nullptr;
}

inline bool parseDoubleAfter(const char* line, const char* key, double& out) noexcept {
    const char* p = findToken(line, key);
    if (!p) return false;
    p += std::strlen(key);
    char* end = nullptr;
    const double v = std::strtod(p, &end);
    if (end == p || !std::isfinite(v)) return false;
    out = v;
    return true;
}

inline bool parseLongAfter(const char* line, const char* key, long long& out) noexcept {
    const char* p = findToken(line, key);
    if (!p) return false;
    p += std::strlen(key);
    char* end = nullptr;
    const long long v = std::strtoll(p, &end, 10);
    if (end == p) return false;
    out = v;
    return true;
}

inline bool parseClockAfter(const char* line, char* out, size_t outSz) noexcept {
    const char* p = findToken(line, "clock=");
    if (!p) return false;
    p += 6;
    size_t i = 0;
    while (p[i] != '\0' && p[i] != ' ' && p[i] != '\t' && p[i] != '\n' && i + 1 < outSz) {
        out[i] = p[i];
        ++i;
    }
    out[i] = '\0';
    return i > 0;
}

} // namespace write_stage_detail

// Parsea la primera línea del dump. ageMs = antigüedad medida por el
// llamador (readWriteStageAsrc la calcula del mtime). Devuelve false sin
// tocar `out` (salvo valid=false) si algo no cuadra.
inline bool parseWriteStageAsrcDumpLine(const char* line, uint64_t ageMs,
                                        WriteStageAsrcSample& out) noexcept {
    out = WriteStageAsrcSample{};
    out.ageMs = ageMs;
    if (!line) return false;
    if (std::strncmp(line, kWriteStageAsrcDumpHeader,
                     std::strlen(kWriteStageAsrcDumpHeader)) != 0) return false;
    if (ageMs > kWriteStageAsrcMaxAgeMs) return false;

    WriteStageAsrcSample s;
    s.ageMs = ageMs;
    long long v = 0;
    double d = 0.0;
    if (!write_stage_detail::parseLongAfter(line, "pid=", v) || v <= 0) return false;
    s.pid = static_cast<int>(v);
    if (!write_stage_detail::parseLongAfter(line, "enabled=", v)) return false;
    s.enabled = (v != 0);
    if (!write_stage_detail::parseDoubleAfter(line, "ratio=", d) ||
        !std::isfinite(d) || d < 0.999 || d > 1.001) return false;
    s.ratio = d;
    if (!write_stage_detail::parseDoubleAfter(line, "drift_ppm=", d) ||
        !std::isfinite(d) || std::fabs(d) > kWriteStageAsrcMaxDriftPpm) return false;
    s.driftPpm = d;
    if (!write_stage_detail::parseDoubleAfter(line, "kalman_drift=", d) ||
        !std::isfinite(d) || std::fabs(d) > kWriteStageAsrcMaxDriftPpm) return false;
    s.kalmanDriftPpm = d;
    if (!write_stage_detail::parseClockAfter(line, s.clock, sizeof(s.clock))) return false;
    if (std::strcmp(s.clock, "mmap") != 0 &&
        std::strcmp(s.clock, "write-timing") != 0 &&
        std::strcmp(s.clock, "unknown") != 0) return false;
    if (!write_stage_detail::parseLongAfter(line, "opens=", v) || v < 0) return false;
    s.opens = static_cast<uint64_t>(v);
    if (!write_stage_detail::parseLongAfter(line, "ok=", v) || v < 0) return false;
    s.ok = static_cast<uint64_t>(v);
    if (!write_stage_detail::parseLongAfter(line, "sat=", v)) return false;
    s.sat = static_cast<int>(v);
    if (!write_stage_detail::parseLongAfter(line, "fir_taps=", v)) return false;
    s.firTaps = static_cast<int>(v);
    if (s.firTaps != kWriteStageAsrcFirTapsV5) return false;
    if (!write_stage_detail::parseLongAfter(line, "neon=", v)) return false;
    s.neon = static_cast<int>(v);
    s.valid = true;
    out = s;
    return true;
}

// Lee el dump del dispositivo y parsea su primera línea. Solo lectura:
// open(O_RDONLY), sin crear ni modificar nada. Devuelve valid=false si el
// archivo no existe, no se puede leer, está viejo o no pasa la validación.
inline bool readWriteStageAsrc(WriteStageAsrcSample& out,
                               const char* path = kWriteStageAsrcDumpPath,
                               uint64_t maxAgeMs = kWriteStageAsrcMaxAgeMs) noexcept {
    out = WriteStageAsrcSample{};
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return false;
    }
    struct timespec now {};
    if (::clock_gettime(CLOCK_REALTIME, &now) != 0) {
        ::close(fd);
        return false;
    }
    const int64_t ageMs = (static_cast<int64_t>(now.tv_sec) - static_cast<int64_t>(st.st_mtime)) * 1000LL +
                          (static_cast<int64_t>(now.tv_nsec) - static_cast<int64_t>(st.st_mtim.tv_nsec)) / 1000000LL;
    if (ageMs < 0 || static_cast<uint64_t>(ageMs) > maxAgeMs) {
        ::close(fd);
        return false;
    }
    char line[4096]{};
    const ssize_t n = ::read(fd, line, sizeof(line) - 1);
    ::close(fd);
    if (n <= 0) return false;
    line[n] = '\0';
    // Solo la primera línea: el resto son líneas STREAM por stream.
    if (char* nl = std::strchr(line, '\n')) *nl = '\0';
    return parseWriteStageAsrcDumpLine(line, static_cast<uint64_t>(ageMs), out);
}

} // namespace ivanna::audio
