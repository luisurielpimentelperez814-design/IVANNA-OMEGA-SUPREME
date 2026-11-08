// =====================================================================
//  IVANNA — DoP v1.1 (DSD over PCM) v2.5.368
//  DopPacketizer.hpp — header-only, C++20/26, cero malloc.
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  El camino real hacia "Zero-PCM / DSD directo" (pilar 5,
//  docs/AUX_GENEZI_ROUTE.md §5). El "DSD nativo directo al DAC" por
//  USB no existe en el stack de Android, pero el estándar abierto
//  DoP v1.1 (Koch / McHarg / Robinson, 2012, "Method for transferring
//  DSD Audio over PCM Frames") SÍ permite llevar el bitstream DSD
//  intacto por el transporte PCM existente (USB Audio Class 2,
//  S/PDIF, AES3) sin reescribir drivers:
//
//    * Cada muestra PCM de 24 bits lleva 16 bits de DSD en los bits
//      15..0 (el bit más antiguo en el bit 15) y un marcador en los
//      bits 23..16 que alterna 0x05 / 0xFA en cada muestra.
//    * El DAC que soporta DoP detecta la alternancia y encamina los
//      16 bits a su decodificador DSD; si el patrón se rompe, vuelve
//      a PCM. Los bits DSD llegan bit a bit idénticos.
//    * La frecuencia PCM portadora es DSD_rate / 16: DSD64 ->
//      176.4 kHz, DSD128 -> 352.8 kHz, DSD256 -> 705.6 kHz.
//    * Cada muestra PCM de un canal lleva SOLO DSD de ese canal.
//
//  Límites honestos (no se finge nada):
//    * Esto es TRANSPORTE, no conversión: necesita una fuente DSD
//      (archivo DSF/DFF) y un DAC que declare soporte DoP. Sin DAC
//      DoP, el flujo se oye como PCM con ruido (el marcador suena).
//    * La conversión PCM -> DSD (modulador sigma-delta, lo que hace
//      HQPlayer) NO está aquí: es trabajo futuro. Este módulo
//      empaqueta un bitstream DSD que ya existe.
//    * La salida Aux 3.5 mm sigue siendo analógica después del DAC:
//      el DoP vive en el tramo digital USB -> DAC, no en el cable.
//    * Si se aplica DSP (volumen, EQ) al flujo DoP, el marcador se
//      destruye: el DoP debe ir directo al DAC, sin tocarlo.
//
//  API: pack() empaqueta bytes DSD por canal en muestras PCM de 24
//  bits (en int32). La fase del marcador (0x05/0xFA) se conserva
//  entre llamadas; reset() la reinicia a 0x05.
// =====================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace ivanna::spatial {

enum class DopRate : int {
    Dsd64 = 64,    // 2.8224 MHz -> portadora 176.4 kHz
    Dsd128 = 128,  // 5.6448 MHz -> portadora 352.8 kHz
    Dsd256 = 256,  // 11.2896 MHz -> portadora 705.6 kHz
};

inline constexpr std::uint32_t dopCarrierSampleRateHz(DopRate r) noexcept {
    switch (r) {
        case DopRate::Dsd128: return 352800;
        case DopRate::Dsd256: return 705600;
        case DopRate::Dsd64:
        default: return 176400;
    }
}

// Marcadores DoP v1.1 (byte alto, bits 23..16). Alternan cada muestra.
inline constexpr std::uint8_t kDopMarkerA = 0x05;
inline constexpr std::uint8_t kDopMarkerB = 0xFA;

class DopPacketizer {
public:
    void reset() noexcept { markerA_ = true; }

    // Empaqueta bytes DSD por canal (mismo número de bytes en L y R).
    // pcmOut debe tener sitio para frames*2 int32 (L/R entrelazado).
    // Devuelve el número de muestras PCM por canal escritas, o 0 si
    // los punteros son nulos o no hay bytes. Si bytesPerChannel es
    // impar, el último byte se descarta (un frame DoP lleva 2 bytes).
    // La fase del marcador se conserva entre llamadas.
    std::size_t pack(const std::uint8_t* dsdLeft,
                     const std::uint8_t* dsdRight,
                     std::size_t bytesPerChannel,
                     std::int32_t* pcmOut) noexcept {
        if (!dsdLeft || !dsdRight || !pcmOut) return 0;
        const std::size_t frames = bytesPerChannel / 2;
        for (std::size_t f = 0; f < frames; ++f) {
            const std::uint8_t marker =
                markerA_ ? kDopMarkerA : kDopMarkerB;
            markerA_ = !markerA_;
            const std::uint32_t payloadL =
                (static_cast<std::uint32_t>(dsdLeft[2 * f]) << 8) |
                static_cast<std::uint32_t>(dsdLeft[2 * f + 1]);
            const std::uint32_t payloadR =
                (static_cast<std::uint32_t>(dsdRight[2 * f]) << 8) |
                static_cast<std::uint32_t>(dsdRight[2 * f + 1]);
            pcmOut[2 * f] = static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(marker) << 16) | payloadL);
            pcmOut[2 * f + 1] = static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(marker) << 16) | payloadR);
        }
        return frames;
    }

    [[nodiscard]] bool nextMarkerIsA() const noexcept { return markerA_; }

private:
    bool markerA_ = true;  // el primer frame lleva 0x05
};

}  // namespace ivanna::spatial
