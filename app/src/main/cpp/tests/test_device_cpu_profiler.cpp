// test_device_cpu_profiler.cpp — Test standalone del perfilador nativo.
//
// Con su propio main() (sin gtest, sin dependencias del resto del árbol),
// mismo patrón que test_adaptive_engine. Se compila en host con:
//   g++ -std=c++17 -Wall -Wextra -I app/src/main/cpp
//     app/src/main/cpp/tests/test_device_cpu_profiler.cpp
//     -o /tmp/test_device_cpu_profiler -pthread
// Registro en CI (1 línea, para el integrador):
//   ivanna_add_test(test_device_cpu_profiler)
// (Este archivo NO toca tests/CMakeLists.txt.)

#include "device_cpu_profiler.hpp"

#include <cstdio>

namespace {
int g_failures = 0;
int g_checks = 0;

#define EXPECT(cond)                                                    \
    do {                                                                \
        ++g_checks;                                                     \
        if (!(cond)) {                                                  \
            ++g_failures;                                               \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                               \
    } while (0)

// Muestra sintética de /proc/cpuinfo de un big.LITTLE 2+6 (como el G85:
// 2× Cortex-A78 @2.3 GHz + 6× Cortex-A55 @2.0 GHz). En el host las
// frecuencias se leen de /sys (valores reales del host, no de la muestra),
// así que aquí solo se afirman modelo/implementer/part/número de núcleos.
const char* kBigLittleCpuinfo =
    "processor\t: 0\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd05\n"
    "\n"
    "processor\t: 1\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd05\n"
    "\n"
    "processor\t: 2\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd41\n"
    "\n"
    "processor\t: 3\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd41\n"
    "\n"
    "processor\t: 4\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd41\n"
    "\n"
    "processor\t: 5\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd41\n"
    "\n"
    "processor\t: 6\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd41\n"
    "\n"
    "processor\t: 7\n"
    "model name\t: ARMv8 Processor rev 4 (v8l)\n"
    "CPU implementer\t: 0x41\n"
    "CPU part\t: 0xd41\n"
    "\n"
    "Hardware\t: Qualcomm Technologies, Inc SM6375-AC\n";
}  // namespace

int main() {
    using namespace ivanna::device;

    // 1. Conteo de CPUs en línea: siempre ≥ 1, nunca 0 ni negativo.
    EXPECT(online_cpu_count() >= 1);

    // 2. SIMD en host x86_64: SSE2 debe detectarse (todo x86_64 lo tiene).
    //    En ARM el test corre en CI de dispositivo; aquí solo se exige que
    //    la detección no mienta en el host.
#if defined(__x86_64__)
    const SimdCaps caps = detect_simd();
    EXPECT(caps.sse2);
    EXPECT(!caps.neon);  // En x86 no hay NEON: false, no inventado.
#endif

    // 3. Parseo de /proc/cpuinfo sintético big.LITTLE 2+6.
    const std::vector<CpuCore> cores = parse_cpuinfo(kBigLittleCpuinfo);
    EXPECT(cores.size() == 8);
    if (cores.size() == 8) {
        EXPECT(cores[0].index == 0);
        EXPECT(cores[7].index == 7);
        EXPECT(cores[0].implementer == 0x41);  // ARM
        EXPECT(cores[0].part == 0xd05);         // Cortex-A55
        EXPECT(cores[2].part == 0xd41);         // Cortex-A78
        EXPECT(cores[2].modelName == "ARMv8 Processor rev 4 (v8l)");
    }

    // 4. Texto vacío o sin bloques → vector vacío (nunca crash, nunca basura).
    EXPECT(parse_cpuinfo("").empty());
    EXPECT(parse_cpuinfo("Hardware\t: Foo\n").empty());
    EXPECT(parse_cpuinfo("processor\t: notanumber\n").size() == 1);  // fallback a índice secuencial

    // 5. Clasificación de clústeres con frecuencias fijas (determinista).
    std::vector<CpuCore> synth(8);
    for (int i = 0; i < 8; ++i) {
        synth[i].index = i;
        synth[i].maxFreqKhz = (i < 2) ? 2300000 : 2000000;  // 2 grandes + 6 pequeños
    }
    const std::vector<int> clusters = classify_clusters(synth);
    EXPECT(clusters.size() == 8);
    if (clusters.size() == 8) {
        EXPECT(clusters[0] == 0);  // BIG
        EXPECT(clusters[1] == 0);  // BIG
        EXPECT(clusters[2] == 2);  // LITTLE
        EXPECT(clusters[7] == 2);  // LITTLE
    }

    // 6. Sin frecuencias (o una sola distinta) no se clasifica: UNKNOWN (-1).
    std::vector<CpuCore> noFreq(4);
    for (int i = 0; i < 4; ++i) noFreq[i].index = i;
    const std::vector<int> unknown = classify_clusters(noFreq);
    EXPECT(unknown.size() == 4);
    for (int c : unknown) EXPECT(c == -1);

    // 7. Afinidad: fijar el hilo actual al núcleo 0 debe aceptarse en Linux;
    //    un índice fuera de rango debe rechazarse sin crash.
#if defined(__linux__)
    EXPECT(set_thread_affinity({0}));
    EXPECT(!set_thread_affinity({CPU_SETSIZE + 1}));
    EXPECT(!set_thread_affinity({-1}));
#endif

    // 8. Lectura de frecuencia de un CPU inexistente → -1, no crash.
    EXPECT(read_cpu_max_freq_khz(9999) == -1);

    std::printf("test_device_cpu_profiler: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
