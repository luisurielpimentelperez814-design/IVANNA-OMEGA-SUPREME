// =====================================================================
//  IVANNA — Fusión Homogénea: el Hiper-Operador de Estado Φ v2.5.367
//  HomogeneousFusion.hpp — header-only, C++20/26, cero malloc en audio.
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  Qué es: la fusión de todo lo construido (bucle neuro cerrado v2.5.365
//  + ruta AUX Genezi/auto v2.5.366 + HOLOS) en UN solo coordinador con
//  UN solo estado, en lugar de tres piezas sueltas que cada ruta
//  cableaba por separado.
//
//  El operador Φ es una COMPOSICIÓN de operadores reales que ya existen,
//  en el orden en que ya corren en la cadena:
//
//      Φ = Γ ∘ K ∘ G ∘ XTC ∘ AUX ∘ N
//
//    N   = ClosedLoopController + NeuroBandEq (bucle neuro, v2.5.365):
//          firma cortical por usuario -> ganancia/fase por banda
//          Greenwood + ajustes HOLOS/ORBE, slew-limitado, fail-closed.
//    AUX = AuxFieldCoherence (graves coherentes en fase, LR4).
//    XTC = CrosstalkCanceller (diafonía directa, geometría del par).
//    G   = HolosGreenKernel: Y_b = G_b ⊛ [K_b · X_b] · Γ_b (v2.5.364).
//    K   = HolosVolterraKernel / VolterraH2Symmetric (K=64, kernel
//          MEDIDO GENEZY01; sin kernel, identidad).
//    Γ   = HolosGainField (ganancia única estática × dinámica).
//
//  Φ NO es un tensor continuo sin discretizar: en este stack la señal
//  sigue siendo PCM en bloques de 128 muestras y HOLOS trabaja en STFT
//  (hop 128 / FFT 512, 10.7 ms @48 kHz). No hay procesamiento "sin
//  Fourier por bloques" ni "sin PCM": AudioFlinger y USB Audio Class
//  son PCM, y el Aux 3.5 mm es analógico después del DAC. Lo que Φ sí
//  hace es unificar el ESTADO y la DECISIÓN (un solo update() por
//  bloque, un solo status) en vez de dejar cada término en su propio
//  mundo. Ver docs/HOMOGENEOUS_FUSION.md.
//
//  Los tres vectores de entrada del encargo, estado honesto:
//    S_neuro(t): REAL cuando hay driver (NeuroFrame EEG/ABR/fNIRS).
//      Sin driver no llegan frames y N es identidad (fail-closed).
//    Z_emf(t):  NO DISPONIBLE. Back-EMF por Aux es imposible (ver
//      docs/AUX_GENEZI_ROUTE.md §2). La interfaz existe como
//      EmfVector con available=false; la identificación de planta
//      real es RoomProbe (micrófono). Nada en este módulo finge
//      medir la bobina.
//    W(r,θ):   REAL y acotado: geometría XTC + campo AUX del preset
//      de ruta. No colapsa paredes (ver v2.5.366 §3): la apertura es
//      la del par de altavoces y su geometría, no infinita.
//
//  Tiempo real: update() corre en el hilo de audio, 1× por bloque.
//  ingestNeuro() y loadVolterraPackage() son hilo de control (el
//  paquete IVK1 validado se copia en el hilo de control; cero malloc
//  en update()).
// =====================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../neuromorphic/neuro_acoustic_closed_loop.hpp"
#include "AuxGeneziRoute.hpp"

namespace ivanna::spatial {

// ── Vector de impedancia electromecánica (Z_emf) ─────────────────────────
// NO DISPONIBLE en este stack: la salida Aux 3.5 mm es de solo salida,
// el DAC no mide carga y USB Audio Class no expone sensado. Se deja
// como interfaz explícita (available=false, siempre) para que ningún
// código posterior pueda "olvidar" que este término es identidad, y
// para que la identificación de planta real (RoomProbe, micrófono)
// tenga su propio sitio. Ver docs/AUX_GENEZI_ROUTE.md §2.
struct EmfVector {
    static constexpr bool kAvailable = false;
    float microVolts = 0.0f;  // siempre 0: no hay fuente de medida
};

// ── Estado unificado que update() devuelve cada bloque ───────────────────
struct FusionStatus {
    bool enabled = false;
    bool neuroActive = false;   // bucle neuro ajustando (fresco + confianza)
    bool neuroFresh = false;    // frame neuro dentro de 250 ms
    float neuroWet = 0.0f;      // wet aplicado (slew-limitado) [0..1]
    bool volterraReady = false;  // paquete IVK1 medido validado y guardado
    bool emfAvailable = EmfVector::kAvailable;  // siempre false (ver arriba)
    AuxPresetKind routePreset = AuxPresetKind::GeneziRoom;
    bool auxActive = false;  // campo AUX coherente activo (preset aplicado)
    // Términos que este módulo NO aplica (viven en la cadena que lo usa):
    // XTC y AUX se leen de routeParams(); G/K/Γ son HOLOS (v2.5.364).
};

// ── HomogeneousFusion: un solo estado, un solo update() por bloque ───────
class HomogeneousFusion {
public:
    void prepare(float sampleRate, int blockSize) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        blockSize_ = (blockSize > 0) ? blockSize : 128;
        neuro_.prepare(sampleRate_, blockSize_);
        bandEq_.prepare(sampleRate_);
        volterraPackage_.clear();
        volterraReady_ = false;
        enabled_ = false;
    }

    void setEnabled(bool on) noexcept { enabled_ = on; }

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }

    // Ruta AUX Genezi / auto (v2.5.366): el preset completo (geometría
    // XTC + campo AUX) pasa a ser parte del estado Φ. Hilo de control.
    void setRoutePreset(AuxPresetKind kind) noexcept {
        routePreset_ = kind;
        routeParams_ = auxPreset(kind);
    }

    [[nodiscard]] AuxRouteParams routeParams() const noexcept {
        return routeParams_;
    }

    [[nodiscard]] AuxPresetKind routePreset() const noexcept {
        return routePreset_;
    }

    // Vector S_neuro(t): delega al controlador del bucle cerrado.
    // Hilo del sensor. false = frame rechazado (kind no real, calidad
    // baja, valores fuera de rango) o fusión apagada.
    bool ingestNeuro(const ivanna::neuromorphic::NeuroFrame& frame) noexcept {
        if (!enabled_) return false;
        return neuro_.ingest(frame);
    }

    // Vector Z_emf(t): sin fuente. Devuelve el vector identidad y deja
    // constancia en el status. Nunca finge una medida.
    [[nodiscard]] EmfVector emfVector() const noexcept { return EmfVector{}; }

    // Puerta Volterra (v2.5.366): valida el paquete IVK1 con
    // AuxVolterraGate y, solo si es Ok, guarda una copia para que los
    // motores Volterra de las rutas lo carguen. Hilo de control.
    // Devuelve el motivo del rechazo (Ok = guardado).
    ivanna::dsp::VolterraLoadStatus loadVolterraPackage(
        const std::uint8_t* package, std::size_t size) noexcept {
        const auto st = AuxVolterraGate::validate(package, size, false);
        if (st != ivanna::dsp::VolterraLoadStatus::Ok) {
            volterraPackage_.clear();
            volterraReady_ = false;
            return st;
        }
        volterraPackage_.assign(package, package + size);
        volterraReady_ = true;
        return st;
    }

    [[nodiscard]] bool volterraReady() const noexcept { return volterraReady_; }

    // Paquete validado guardado (para que el motor VolterraH2Symmetric
    // de cada ruta lo cargue con loadKernelPackage). nullptr si no hay.
    [[nodiscard]] const std::uint8_t* volterraPackage() const noexcept {
        return volterraPackage_.empty() ? nullptr : volterraPackage_.data();
    }

    [[nodiscard]] std::size_t volterraPackageSize() const noexcept {
        return volterraPackage_.size();
    }

    // Hilo de audio, 1× por bloque, ANTES de HOLOS/ORBE (misma posición
    // que el updateNeuroLoop de v2.5.365, al que sustituye).
    //   base: objetivos HOLOS/ORBE actuales de la cadena (se leen de los
    //     atomics del usuario cada bloque; nunca se reescriben).
    //   adjustedOut: base con los ajustes neuro aplicados (identidad si
    //     el bucle no está activo: fail-closed).
    //   L/R: la ganancia neuro por banda se aplica en muestras con
    //     NeuroBandEq (bypass bit-exacto si |ganancia| < 0.01 dB).
    // XTC, AUX, G, K y Γ NO se aplican aquí: viven en la cadena y leen
    // sus parámetros de routeParams() / HOLOS. Este método es el punto
    // único de decisión, no una segunda cadena de DSP.
    FusionStatus update(float* left, float* right, int n,
                        const ivanna::neuromorphic::ClosedLoopTargets& base,
                        ivanna::neuromorphic::ClosedLoopTargets& adjustedOut,
                        std::uint64_t nowUs = 0) noexcept {
        FusionStatus st{};
        st.enabled = enabled_;
        st.routePreset = routePreset_;
        st.auxActive = enabled_ && routeParams_.auxFieldEnabled;
        st.volterraReady = volterraReady_;
        st.emfAvailable = EmfVector::kAvailable;
        adjustedOut = base;
        if (!enabled_) return st;
        if (!left || !right || n <= 0) return st;

        const auto adj = neuro_.compute(nowUs);
        st.neuroActive = adj.active;
        st.neuroWet = adj.wetScale;
        st.neuroFresh = adj.active;  // active implica fresco + confianza
        if (adj.active) {
            bandEq_.setGains(adj.gainDb, sampleRate_);
            bandEq_.process(left, right, n);
            adjustedOut = ivanna::neuromorphic::applyAdjustments(base, adj);
        } else {
            // Decaimiento a identidad: las ganancias hacen slew a 0 en
            // compute(); el EQ queda en bypass bit-exacto.
            bandEq_.setGains(adj.gainDb, sampleRate_);
        }
        return st;
    }

private:
    float sampleRate_ = 48000.0f;
    int blockSize_ = 128;
    bool enabled_ = false;
    ivanna::neuromorphic::ClosedLoopController neuro_{};
    ivanna::neuromorphic::NeuroBandEq bandEq_{};
    AuxPresetKind routePreset_ = AuxPresetKind::GeneziRoom;
    AuxRouteParams routeParams_ = auxPreset(AuxPresetKind::GeneziRoom);
    std::vector<std::uint8_t> volterraPackage_{};
    bool volterraReady_ = false;
};

}  // namespace ivanna::spatial
