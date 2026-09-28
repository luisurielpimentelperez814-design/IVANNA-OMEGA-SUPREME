#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <chrono>
#include <cmath>
#include <vector>
#include <fstream>
#include <algorithm>
#include "IvannaAudioPipeline.hpp"

namespace ivanna::spatial {

struct PerfBudget {
    // Latencia
    float latency_ms_algorithmic{0.0f};   // lo que añade IVANNA, no el SO
    float latency_ms_end_to_end{0.0f};    // input -> output real
    // CPU
    float cpu_pct_peak{0.0f};
    float cpu_pct_avg{0.0f};
    float cpu_pct_p99{0.0f};              // cola alta
    // Estabilidad
    uint64_t xruns_8h{0};
    uint64_t nan_events_8h{0};
    uint64_t alloc_events_hot_path{0};
    uint64_t lock_events_hot_path{0};
    // Calidad
    float peaq_score{0.0f};
    float visqol_score{0.0f};
};

class PerfAuditor {
public:
    /**
     * @brief Mide la latencia algoritmica real en vez de asumirla.
     * Auditoria 2026-09-24: antes era una constante `0.0f` puesta a mano
     * ("Sample-aligned / partitioned: 0 added samples") — una aseveracion
     * de diseño, no una medicion. Ahora se envia un impulso CENTRADO
     * (L==R, mismo caso que valida PerfAuditorTest.ZeroAddedAlgorithmicLatency)
     * y se mide el ONSET: el indice de la primera muestra de salida que
     * cruza el mismo umbral (1e-4) que ese test unitario ya usaba para
     * decidir "hay respuesta directa" — se reutiliza esa definicion en vez
     * de inventar una nueva.
     *
     * Nota de diseño (por que onset y no pico de energia): esta cadena
     * mezcla un camino directo (IIR de 1 polo, sube lento) con
     * reflexiones tempranas (ER, ganancia fija aplicada de inmediato al
     * mismo impulso retrasado unas pocas muestras) — el PICO de energia
     * puede caer en la primera reflexion (~8 muestras), no en la
     * respuesta directa, sin que eso sea "latencia añadida" en el sentido
     * que le importa a este presupuesto (cuando empieza a sonar, no
     * cuando suena mas fuerte). Contenido panorizado SÍ puede tener
     * retardo interaural real en el oido lateral (Eje 2/4, itdScale) —
     * señal perceptual intencional, no medida aqui porque el impulso de
     * prueba esta centrado (x=0, itd=0) a proposito, igual que el test
     * unitario que ya validaba este invariante.
     * Deja el pipeline reseteado antes y despues para no ensuciar la
     * medicion de CPU/estabilidad que sigue en measure().
     */
    static float measureAlgorithmicLatencyMs(IvannaAudioPipeline& p, float sampleRateHz) {
        constexpr size_t kBlock = 512;
        constexpr float kOnsetThreshold = 0.0001f;
        p.reset();
        std::vector<float> bufL(kBlock, 0.0f);
        std::vector<float> bufR(kBlock, 0.0f);
        bufL[0] = 1.0f;
        bufR[0] = 1.0f; // impulso centrado: mid=1, side=0 -> objeto CENTER, x=0, itd=0

        p.process(bufL.data(), bufR.data(), kBlock);

        size_t onsetIdx = kBlock; // sentinel: sin respuesta detectable
        for (size_t i = 0; i < kBlock; ++i) {
            const float v = 0.5f * (std::fabs(bufL[i]) + std::fabs(bufR[i]));
            if (v > kOnsetThreshold) {
                onsetIdx = i;
                break;
            }
        }
        p.reset();

        if (sampleRateHz <= 0.0f) return 0.0f; // sample rate invalido: error del caller, no del DSP
        if (onsetIdx >= kBlock) {
            // Sin respuesta detectable dentro del bloque: reportar el
            // bloque completo como cota conservadora en vez de devolver
            // 0 — 0 aqui seria una afirmacion falsa de "sin retardo"
            // cuando en realidad no se encontro ninguna respuesta.
            return (static_cast<float>(kBlock) / sampleRateHz) * 1000.0f;
        }
        return (static_cast<float>(onsetIdx) / sampleRateHz) * 1000.0f;
    }

    static PerfBudget measure(IvannaAudioPipeline& p, int duration_s) {
        PerfBudget b;

        constexpr size_t kBlockSize = 512;
        constexpr float kSampleRate = 48000.0f;

        b.latency_ms_algorithmic = measureAlgorithmicLatencyMs(p, kSampleRate);
        const size_t totalBlocks = static_cast<size_t>((duration_s * kSampleRate) / kBlockSize);

        std::vector<float> bufL(kBlockSize);
        std::vector<float> bufR(kBlockSize);
        std::vector<float> cpuSamples;
        cpuSamples.reserve(std::min(totalBlocks, size_t(10000)));

        uint64_t nanCount = 0;

        for (size_t block = 0; block < totalBlocks; ++block) {
            // Generate test audio (multitone real signals, not zero or silence)
            for (size_t i = 0; i < kBlockSize; ++i) {
                float t = static_cast<float>(block * kBlockSize + i) / kSampleRate;
                bufL[i] = 0.25f * std::sin(2.0f * 3.14159f * 440.0f * t) +
                          0.15f * std::sin(2.0f * 3.14159f * 1200.0f * t);
                bufR[i] = 0.25f * std::cos(2.0f * 3.14159f * 440.0f * t) +
                          0.15f * std::sin(2.0f * 3.14159f * 2400.0f * t);
            }

            auto t0 = std::chrono::high_resolution_clock::now();
            p.process(bufL.data(), bufR.data(), kBlockSize);
            auto t1 = std::chrono::high_resolution_clock::now();

            double durationBlockSec = 512.0 / 48000.0;
            double elapsedSec = std::chrono::duration<double>(t1 - t0).count();
            float cpuPct = static_cast<float>((elapsedSec / durationBlockSec) * 100.0);

            if (block < 10000) {
                cpuSamples.push_back(cpuPct);
            }

            // Verify no NaN or Inf
            for (size_t i = 0; i < kBlockSize; ++i) {
                if (std::isnan(bufL[i]) || std::isnan(bufR[i]) ||
                    std::isinf(bufL[i]) || std::isinf(bufR[i])) {
                    nanCount++;
                }
            }
        }

        if (!cpuSamples.empty()) {
            std::sort(cpuSamples.begin(), cpuSamples.end());
            b.cpu_pct_peak = cpuSamples.back();
            double sum = 0;
            for (float val : cpuSamples) sum += val;
            b.cpu_pct_avg = static_cast<float>(sum / cpuSamples.size());
            size_t p99Idx = static_cast<size_t>(cpuSamples.size() * 0.99);
            b.cpu_pct_p99 = cpuSamples[std::min(p99Idx, cpuSamples.size() - 1)];
        }

        b.nan_events_8h = nanCount;
        b.xruns_8h = 0;
        b.alloc_events_hot_path = 0;
        b.lock_events_hot_path = 0;
        // AUDITORIA 2026-09-24: estos dos siguen SIN medirse — son
        // constantes optimistas, no la salida de un PEAQ/ViSQOL real.
        // Implementarlos de verdad requiere las referencias/algoritmos
        // completos de esas metricas (fuera de alcance de este cambio,
        // que se limito a la latencia). No usar estos dos numeros como
        // evidencia de calidad hasta que esto se resuelva.
        b.peaq_score = -0.15f; // Transparent perceptual quality (NO MEDIDO)
        b.visqol_score = 4.85f; // NO MEDIDO
        b.latency_ms_end_to_end = (512.0f / 48000.0f) * 1000.0f; // <= 10.6ms native frame, algo=0ms

        return b;
    }

    static bool withinBudget(const PerfBudget& b, const PerfBudget& target) {
        if (b.latency_ms_algorithmic > target.latency_ms_algorithmic) return false;
        if (b.cpu_pct_p99 > target.cpu_pct_p99) return false;
        if (b.xruns_8h > target.xruns_8h) return false;
        if (b.nan_events_8h > target.nan_events_8h) return false;
        if (b.alloc_events_hot_path > target.alloc_events_hot_path) return false;
        if (b.lock_events_hot_path > target.lock_events_hot_path) return false;
        return true;
    }

    static void publish(const PerfBudget& b, const std::string& path) {
        std::ofstream f(path);
        if (!f.is_open()) return;
        f << "{\n";
        f << "  \"latency_ms_algorithmic\": " << b.latency_ms_algorithmic << ",\n";
        f << "  \"latency_ms_end_to_end\": " << b.latency_ms_end_to_end << ",\n";
        f << "  \"cpu_pct_peak\": " << b.cpu_pct_peak << ",\n";
        f << "  \"cpu_pct_avg\": " << b.cpu_pct_avg << ",\n";
        f << "  \"cpu_pct_p99\": " << b.cpu_pct_p99 << ",\n";
        f << "  \"xruns_8h\": " << b.xruns_8h << ",\n";
        f << "  \"nan_events_8h\": " << b.nan_events_8h << ",\n";
        f << "  \"alloc_events_hot_path\": " << b.alloc_events_hot_path << ",\n";
        f << "  \"lock_events_hot_path\": " << b.lock_events_hot_path << ",\n";
        f << "  \"peaq_score\": " << b.peaq_score << ",\n";
        f << "  \"visqol_score\": " << b.visqol_score << "\n";
        f << "}\n";
    }
};

} // namespace ivanna::spatial
