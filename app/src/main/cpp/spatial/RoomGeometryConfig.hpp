// RoomGeometryConfig.hpp — geometría 3D real de sala para Wave Field Synthesis
// y constantes físico-acústicas trazables (Prompt Maestro v3.0 §5, §6.2, §6.3, §10).
// Coordenadas en metros, marco de la SALA: X=ancho, Y=altura, Z=profundidad.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace ivanna::spatial {

struct RoomGeometryConfig {
    static constexpr int kNumSpeakers = 7;

    // Constantes físicas nombradas (Prompt Maestro v3.0 §0.5, §5, §6.2, §6.3, §10):
    // Sala real de referencia del usuario (Sony MHC-PZ1D): 7 m × 4 m × 4 m = 112 m³
    // (hormigón + techo de fibrocemento).
    static constexpr float kReferenceRoomWidthM       = 4.0f;
    static constexpr float kReferenceRoomHeightM      = 4.0f;
    static constexpr float kReferenceRoomDepthM       = 7.0f;
    static constexpr float kReferenceRoomVolumeM3     = kReferenceRoomWidthM * kReferenceRoomHeightM * kReferenceRoomDepthM; // 112.0 m³
    static constexpr float kSchroederCoeff            = 2000.0f; // f_s = 2000 * sqrt(T60 / V) [Hz]
    static constexpr float kDryRoomT60CeilingSec      = 0.50f;   // Bajo este T60 se permite 100% del wet sintético pedido
    static constexpr float kLiveRoomT60CutoffSec      = 1.20f;   // §6.2: En sala viva (T60 >= 1.2 s) cola y ER sintéticas OFF (0.0)

    float roomWidthM  = 3.5f;
    float roomDepthM  = 7.0f;
    float roomHeightM = 3.5f;

    float listenerX = 1.75f, listenerY = 1.20f, listenerZ = 3.50f;

    // Orden fijo: FL, FR, SL(lateral izq), SR(lateral der), TL(elevado izq),
    // TR(elevado der), SW(subwoofer).
    std::array<float, kNumSpeakers> speakerX{{0.35f, 3.15f, 0.25f, 3.25f, 0.25f, 3.25f, 3.00f}};
    std::array<float, kNumSpeakers> speakerY{{1.50f, 1.50f, 1.50f, 1.50f, 3.00f, 3.00f, 0.15f}};
    std::array<float, kNumSpeakers> speakerZ{{0.00f, 0.00f, 3.50f, 3.50f, 3.50f, 3.50f, 3.00f}};

    static const RoomGeometryConfig& defaultLayout() noexcept {
        static const RoomGeometryConfig cfg{};
        return cfg;
    }

    // Escenario real de aceptación (§5, §10): Sala 7 × 4 × 4 m (V = 112 m³), Sony MHC-PZ1D.
    static constexpr RoomGeometryConfig referenceSonyMhcPz1dLayout() noexcept {
        RoomGeometryConfig cfg{};
        cfg.roomWidthM  = kReferenceRoomWidthM;
        cfg.roomHeightM = kReferenceRoomHeightM;
        cfg.roomDepthM  = kReferenceRoomDepthM;
        cfg.listenerX   = 2.00f;
        cfg.listenerY   = 1.20f;
        cfg.listenerZ   = 3.50f;
        cfg.speakerX    = {{0.40f, 3.60f, 0.25f, 3.75f, 0.40f, 3.60f, 3.40f}};
        cfg.speakerY    = {{1.40f, 1.40f, 1.50f, 1.50f, 2.80f, 2.80f, 0.20f}};
        cfg.speakerZ    = {{0.00f, 0.00f, 3.50f, 3.50f, 0.20f, 0.20f, 0.30f}};
        return cfg;
    }

    [[nodiscard]] constexpr float volumeM3() const noexcept {
        return std::max(1.0f, roomWidthM * roomDepthM * roomHeightM);
    }

    // §6.3: Frecuencia de Schroeder f_s = 2000 * sqrt(T60 / V)
    // Para V = 112 m³: T60 = 0.35 s -> f_s ≈ 111.80 Hz; T60 = 2.0 s -> f_s ≈ 267.26 Hz.
    [[nodiscard]] static inline float computeSchroederFrequencyHz(float t60Sec, float volumeM3Val) noexcept {
        if (!std::isfinite(t60Sec) || !std::isfinite(volumeM3Val) || t60Sec <= 0.0f || volumeM3Val <= 0.0f) {
            return 0.0f;
        }
        return kSchroederCoeff * std::sqrt(t60Sec / volumeM3Val);
    }

    [[nodiscard]] inline float schroederFrequencyHz(float t60Sec) const noexcept {
        return computeSchroederFrequencyHz(t60Sec, volumeM3());
    }

    // §6.2 & §7.7: Limitador físico de reverberación y reflexiones tempranas sintéticas según T60 de la sala.
    // En sala viva (T60 >= 1.2 s) la cola y las ER sintéticas están OFF (0.0f) por defecto.
    [[nodiscard]] static constexpr float limitSyntheticReverbWetForRoomT60(
        float requestedWet,
        float roomT60Sec) noexcept
    {
        if (requestedWet <= 0.0f) return 0.0f;
        const float clampedWet = std::clamp(requestedWet, 0.0f, 1.0f);
        if (roomT60Sec >= kLiveRoomT60CutoffSec) {
            return 0.0f;
        }
        if (roomT60Sec <= kDryRoomT60CeilingSec) {
            return clampedWet;
        }
        const float scale = (kLiveRoomT60CutoffSec - roomT60Sec) /
                            (kLiveRoomT60CutoffSec - kDryRoomT60CeilingSec);
        return clampedWet * std::clamp(scale, 0.0f, 1.0f);
    }

    // Posición de un altavoz RELATIVA al oyente (marco WFS: x=lateral+der,
    // y=frente(+)/atrás, z=altura respecto al oído). dy usa -(sz-lz) porque
    // el eje "frente" de WfsRenderer crece hacia el oyente desde la sala.
    void speakerRelativeTo(int i, float& dx, float& dyFwd, float& dz) const noexcept {
        dx    = speakerX[static_cast<size_t>(i)] - listenerX;
        dyFwd = listenerZ - speakerZ[static_cast<size_t>(i)];
        dz    = speakerY[static_cast<size_t>(i)] - listenerY;
    }
};

} // namespace ivanna::spatial
