// ivanna_hires_config.hpp — contrato hi-res unico app<->daemon.
// La app (JNI) escribe /data/adb/ivanna_omega/hires.conf; el daemon lo lee al
// arrancar para configurar su SHM al rate pedido. Validacion cerrada:
// rate en {44100,48000,88200,96000,176400,192000,352800,384000}, depth en
// {16,24,32}.
//
// FIX (2026-09-20, reporte real del propietario: cortes/underruns): el
// contrato rechazaba TODA la familia de 44.1 kHz (44100/88200/176400/352800)
// aunque UsbAudioProManager.kt ya la soporta en hardware (ver su lista
// PREFERRED_RATES). La musica de streaming y de CD es 44.1 kHz: forzarla a
// 48000 obliga a un remuestreo 147:160 permanente en la ruta, justo la clase
// de conversion que produce artefactos y consumo extra. Ahora ambas familias
// (44.1k y 48k) pasan tal cual, sin remuestreo.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ivanna { namespace hires {
inline bool isValidRate(int r)  {
    // Familia 48 kHz (video/Android) y familia 44.1 kHz (CD/streaming).
    return r==44100  || r==48000  ||
           r==88200  || r==96000  ||
           r==176400 || r==192000 ||
           r==352800 || r==384000;
}
inline bool isValidDepth(int d) { return d==16 || d==24 || d==32; }
inline bool writeConf(const char* path, int rate, int depth) {
    if (!path || !isValidRate(rate) || !isValidDepth(depth)) return false;
    FILE* f = std::fopen(path, "w"); if (!f) return false;
    std::fprintf(f, "rate=%d\ndepth=%d\n", rate, depth); std::fclose(f); return true;
}
inline bool readConf(const char* path, int& rate, int& depth) {
    if (!path) return false; FILE* f = std::fopen(path, "r"); if (!f) return false;
    char line[64]; int r = 0, d = 0;
    while (std::fgets(line, sizeof line, f)) {
        if      (std::strncmp(line, "rate=", 5) == 0)  r = std::atoi(line + 5);
        else if (std::strncmp(line, "depth=", 6) == 0) d = std::atoi(line + 6);
    }
    std::fclose(f);
    if (isValidRate(r) && isValidDepth(d)) { rate = r; depth = d; return true; }
    return false;
}
}} // namespace ivanna::hires
