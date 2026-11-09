// device_cpu_profiler.hpp — Perfilador de CPU nativo de IVANNA (header-only).
//
// Detecta capacidades SIMD, topología de núcleos y frecuencias máximas sin
// depender del NDK ni de ninguna librería del sistema: solo C/C++ estándar
// + POSIX (unistd, pthread, sys/auxv donde existe). Por eso compila y se
// prueba en host (x86_64) y en el dispositivo (AArch64) con el mismo código.
//
// Uso previsto (futuro puente JNI, NO cableado todavía):
//   - DeviceProfiler (Kotlin) ya cubre /proc/cpuinfo y /sys desde Java.
//   - Este header es la fuente autoritativa para (a) NEON por hardware
//     (getauxval AT_HWCAP, no por ABI) y (b) afinidad de hilos
//     (ivanna_set_thread_affinity) para el actuador THREAD_AFFINITY del
//     HotOptimizer. El JNI que lo exponga es un trabajo pequeño y explícito.
//
// Fail-closed: toda función devuelve un valor seguro ante error (0, false,
// lista vacía) y nunca aborta. Ninguna función escribe en el sistema.
//
// Registro en CI (1 línea, para el integrador — este archivo NO toca
// tests/CMakeLists.txt):
//   ivanna_add_test(test_device_cpu_profiler)
// El test vive en app/src/main/cpp/tests/test_device_cpu_profiler.cpp y se
// compila en host con:  g++ -std=c++17 -Wall -Wextra -I app/src/main/cpp
//   app/src/main/cpp/tests/test_device_cpu_profiler.cpp -o /tmp/test_device_cpu_profiler

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#if defined(__aarch64__) || defined(__arm__)
#include <sys/auxv.h>
#endif
#endif

namespace ivanna::device {

// ── Capacidades SIMD ────────────────────────────────────────────────────────

struct SimdCaps {
    bool neon = false;      // ASIMD (NEON 128-bit) — obligatorio en ARMv8-A.
    bool neonFp16 = false;  // FP16 NEON (ARMv8.2+).
    bool sve = false;       // SVE (ARMv8.2+, raro en teléfonos).
    bool sse2 = false;      // Host x86_64 (para los tests host).
};

// Detecta SIMD por hardware (getauxval AT_HWCAP en Linux/ARM; en host x86
// usa __builtin_cpu_supports). Nunca afirma lo que no midió: si el sistema
// no expone HWCAP, todo queda en false.
inline SimdCaps detect_simd() {
    SimdCaps caps;
#if defined(__linux__) && (defined(__aarch64__) || defined(__arm__))
    const unsigned long hwcap = getauxval(AT_HWCAP);
#ifdef HWCAP_ASIMD
    caps.neon = (hwcap & HWCAP_ASIMD) != 0;
#endif
#ifdef HWCAP_ASIMDHP
    caps.neonFp16 = (hwcap & HWCAP_ASIMDHP) != 0;
#endif
#ifdef HWCAP_SVE
    caps.sve = (hwcap & HWCAP_SVE) != 0;
#endif
#elif defined(__x86_64__) || defined(__i386__)
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    caps.sse2 = __builtin_cpu_supports("sse2") != 0;
#endif
#endif
    return caps;
}

// ── Topología ───────────────────────────────────────────────────────────────

inline int online_cpu_count() {
#if defined(__linux__)
    const long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
#else
    return 1;
#endif
}

struct CpuCore {
    int index = -1;
    std::string modelName;
    int implementer = -1;  // MIDR implementer, p. ej. 0x41 = ARM; -1 = no leído.
    int part = -1;         // MIDR part, p. ej. 0xd05 = Cortex-A55; -1 = no leído.
    long maxFreqKhz = -1;  // -1 = /sys no legible.
};

// Lee /sys/devices/system/cpu/cpuN/cpufreq/scaling_max_freq (kHz). -1 si no
// legible (SELinux lo niega en algunos dispositivos: no es un error).
inline long read_cpu_max_freq_khz(int cpuIndex) {
    char path[128];
    std::snprintf(path, sizeof(path),
                  "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpuIndex);
    FILE* f = std::fopen(path, "r");
    if (f == nullptr) return -1;
    long khz = -1;
    if (std::fscanf(f, "%ld", &khz) != 1) khz = -1;
    std::fclose(f);
    return khz;
}

// Parsea el texto de /proc/cpuinfo en núcleos. En ARM64 cada bloque
// "processor : N" trae model name / CPU implementer / CPU part. Texto vacío
// o sin bloques → vector vacío. (El parser Kotlin espeja esta misma lógica.)
inline std::vector<CpuCore> parse_cpuinfo(const std::string& text) {
    std::vector<CpuCore> cores;
    CpuCore cur;
    bool inBlock = false;
    auto flush = [&]() {
        if (inBlock && cur.index >= 0) {
            cur.maxFreqKhz = read_cpu_max_freq_khz(cur.index);
            cores.push_back(cur);
        }
    };
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t eol = text.find('\n', pos);
        std::string line = text.substr(pos, eol == std::string::npos ? eol : eol - pos);
        pos = (eol == std::string::npos) ? text.size() : eol + 1;
        // Recorta espacios.
        const std::size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line = line.substr(first);
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
        std::string val = line.substr(colon + 1);
        const std::size_t vfirst = val.find_first_not_of(" \t");
        if (vfirst != std::string::npos) val = val.substr(vfirst);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();
        if (key == "processor") {
            flush();
            cur = CpuCore();
            try {
                cur.index = std::stoi(val);
            } catch (...) {
                cur.index = static_cast<int>(cores.size());
            }
            inBlock = true;
        } else if (key == "model name") {
            cur.modelName = val;
        } else if (key == "CPU implementer") {
            try {
                cur.implementer = std::stoi(val, nullptr, 16);
            } catch (...) {
            }
        } else if (key == "CPU part") {
            try {
                cur.part = std::stoi(val, nullptr, 16);
            } catch (...) {
            }
        }
    }
    flush();
    return cores;
}

// Clasifica núcleos en clústeres por frecuencia máxima relativa: la mayor
// es BIG, la menor LITTLE, el resto MID. Sin ≥2 frecuencias distintas no se
// clasifica (cluster UNKNOWN = -1 en el campo de salida paralelo).
// Devuelve un vector paralelo a `cores` con 0=BIG, 1=MID, 2=LITTLE, -1=UNKNOWN.
inline std::vector<int> classify_clusters(const std::vector<CpuCore>& cores) {
    std::vector<int> out(cores.size(), -1);
    long mn = -1, mx = -1;
    for (const auto& c : cores) {
        if (c.maxFreqKhz <= 0) continue;
        if (mn < 0 || c.maxFreqKhz < mn) mn = c.maxFreqKhz;
        if (mx < 0 || c.maxFreqKhz > mx) mx = c.maxFreqKhz;
    }
    if (mn < 0 || mx <= mn) return out;
    for (std::size_t i = 0; i < cores.size(); ++i) {
        const long f = cores[i].maxFreqKhz;
        if (f <= 0) {
            out[i] = -1;
        } else if (f == mx) {
            out[i] = 0;
        } else if (f == mn) {
            out[i] = 2;
        } else {
            out[i] = 1;
        }
    }
    return out;
}

// ── Afinidad de hilos (actuador del HotOptimizer) ───────────────────────────
//
// Fija el hilo actual a los núcleos dados. Devuelve true solo si el sistema
// aceptó el conjunto completo. No escribe en ningún nodo: es una llamada
// POSIX al scheduler del propio proceso (permitida sin root). Si falla, el
// llamador registra UNAVAILABLE y sigue con los valores por defecto.
inline bool set_thread_affinity(const std::vector<int>& cpus) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c : cpus) {
        if (c < 0 || c >= CPU_SETSIZE) return false;
        CPU_SET(c, &set);
    }
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    (void)cpus;
    return false;
#endif
}

}  // namespace ivanna::device
