// ============================================================================
// thermal_governor.hpp — IVANNA Thermal-Aware DSP Governor v1.0
// ============================================================================
// Lee la temperatura del SoC desde sysfs y degrada la calidad DSP de forma
// inteligente antes de que el scheduler térmico del kernel throttlee la CPU:
//
//   < 45°C  : DSP full (todas las etapas activas, NEON vectorizado)
//   45-55°C : reducir Volterra H2 tap length y RIR block size
//   55-65°C : desactivar RIR convolution, mantener HRTF básico
//   65-75°C : solo EQ + Compressor + SafetyLimiter (modo protección)
//   > 75°C  : bypass completo (pass-through con safety clamp)
//
// Esto supera a ViPER4Android y JamesDSP que no tienen gestión térmica:
// bajo carga sostenida sus etapas DSP siguen consumiendo CPU mientras el
// SoC throttlea, produciendo underruns y clics. IVANNA degrada
// graciosamente ANTES del throttling.
//
// Lectura de temperatura: O(1) por bloque via atómica cached + timer
// que actualiza cada 2s en un hilo separado (no en el audio thread RT).
// ============================================================================
#pragma once

#include <atomic>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace ivanna {

enum class ThermalTier : int {
    FULL      = 0,   // < 45°C  — full DSP
    REDUCED   = 1,   // 45-55°C — reducir Volterra + RIR blocks
    LIMITED   = 2,   // 55-65°C — sin RIR convolution
    PROTECTED = 3,   // 65-75°C — solo EQ+Comp+Limiter
    BYPASS    = 4,   // > 75°C  — pass-through
};

struct ThermalProfile {
    bool enableVolterra   = true;
    bool enableRIR        = true;
    bool enableHRTF       = true;
    bool enableNHO        = true;
    bool enableEvoEQ      = true;
    bool enableAntiDolby  = true;
    int  volterraMaxTaps  = 64;   // reducido bajo calor
    int  rirBlockSize     = 512;  // reducido bajo calor
    float dspQualityScale = 1.0f; // [0..1] escala de calidad general
};

class ThermalGovernor {
public:
    ThermalGovernor() {
        // Arrancar hilo de polling térmico (baja prioridad, no RT)
        running_.store(true);
        pollerThread_ = std::thread([this] { thermalPollerLoop(); });
    }

    ~ThermalGovernor() {
        running_.store(false);
        if (pollerThread_.joinable()) pollerThread_.join();
    }

    // Llamado desde el audio thread — O(1), solo lee atómica
    ThermalTier getCurrentTier() const noexcept {
        return static_cast<ThermalTier>(tier_.load(std::memory_order_relaxed));
    }

    float getCurrentTempC() const noexcept {
        return tempC_.load(std::memory_order_relaxed);
    }

    // Retorna perfil DSP para el tier actual
    ThermalProfile getProfile() const noexcept {
        ThermalProfile p;
        switch (getCurrentTier()) {
            case ThermalTier::FULL:
                // Todo activo — defaults
                break;
            case ThermalTier::REDUCED:
                p.volterraMaxTaps = 32;   // mitad de taps
                p.rirBlockSize    = 256;  // bloques más pequeños
                p.dspQualityScale = 0.85f;
                break;
            case ThermalTier::LIMITED:
                p.enableRIR       = false;  // sin convolución de sala
                p.volterraMaxTaps = 16;
                p.dspQualityScale = 0.65f;
                break;
            case ThermalTier::PROTECTED:
                p.enableRIR       = false;
                p.enableVolterra  = false;
                p.enableNHO       = false;
                p.enableAntiDolby = false;
                p.dspQualityScale = 0.40f;
                break;
            case ThermalTier::BYPASS:
                p.enableRIR       = false;
                p.enableVolterra  = false;
                p.enableHRTF      = false;
                p.enableNHO       = false;
                p.enableEvoEQ     = false;
                p.enableAntiDolby = false;
                p.dspQualityScale = 0.0f;
                break;
        }
        return p;
    }

private:
    std::atomic<int>   tier_{0};
    std::atomic<float> tempC_{25.0f};
    std::atomic<bool>  running_{false};
    std::thread        pollerThread_;

    // Tabla de paths de temperatura del SoC conocidos (Snapdragon / Exynos / Tensor)
    static constexpr const char* THERMAL_PATHS[] = {
        // Snapdragon — CPU cluster (más representativo de la carga del audio thread)
        "/sys/class/thermal/thermal_zone4/temp",
        "/sys/class/thermal/thermal_zone7/temp",
        // Tensor (Pixel 6+)
        "/sys/class/thermal/thermal_zone1/temp",
        // Exynos genérico
        "/sys/class/thermal/thermal_zone0/temp",
        // Fallback
        "/sys/devices/virtual/thermal/thermal_zone0/temp",
        nullptr
    };

    float readSocTempC() noexcept {
        for (int i = 0; THERMAL_PATHS[i] != nullptr; ++i) {
            FILE* f = fopen(THERMAL_PATHS[i], "r");
            if (!f) continue;
            int raw = 0;
            int read = fscanf(f, "%d", &raw);
            fclose(f);
            if (read == 1 && raw > 0) {
                // sysfs devuelve milli-grados en la mayoría de kernels
                float temp = (raw > 1000) ? raw / 1000.0f : (float)raw;
                // Sanity: rango plausible de SoC: 20..120°C
                if (temp >= 20.0f && temp <= 120.0f) return temp;
            }
        }
        // No se pudo leer — asumir temperatura segura
        return 35.0f;
    }

    ThermalTier tempToTier(float t) const noexcept {
        if (t < 45.0f) return ThermalTier::FULL;
        if (t < 55.0f) return ThermalTier::REDUCED;
        if (t < 65.0f) return ThermalTier::LIMITED;
        if (t < 75.0f) return ThermalTier::PROTECTED;
        return ThermalTier::BYPASS;
    }

    void thermalPollerLoop() {
        // Hilo de baja prioridad — no interfiere con el audio thread RT
        while (running_.load()) {
            float t = readSocTempC();
            tempC_.store(t, std::memory_order_relaxed);
            tier_.store(static_cast<int>(tempToTier(t)), std::memory_order_relaxed);
            // Polling cada 2s — suficiente para la dinámica térmica de un SoC
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }
};

// Singleton global — inicializado en ivanna_unified_engine o omega_effect
inline ThermalGovernor& getThermalGovernor() {
    static ThermalGovernor instance;
    return instance;
}

} // namespace ivanna
