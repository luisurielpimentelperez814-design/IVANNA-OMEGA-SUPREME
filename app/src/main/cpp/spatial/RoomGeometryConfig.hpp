// RoomGeometryConfig.hpp — geometría 3D real de sala para Wave Field Synthesis.
// Coordenadas en metros, marco de la SALA: X=ancho, Y=altura, Z=profundidad.
#pragma once
#include <array>

namespace ivanna::spatial {

struct RoomGeometryConfig {
    static constexpr int kNumSpeakers = 7;

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
