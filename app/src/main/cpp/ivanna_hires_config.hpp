// ivanna_hires_config.hpp — contrato hi-res unico app<->daemon.
// La app (JNI) escribe /data/adb/ivanna_omega/hires.conf; el daemon lo lee al
// arrancar para configurar su SHM al rate pedido. Validacion cerrada:
// rate en {48000,96000,192000,384000}, depth en {16,24,32}.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace ivanna { namespace hires {
inline bool isValidRate(int r)  { return r==48000 || r==96000 || r==192000 || r==384000; }
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
