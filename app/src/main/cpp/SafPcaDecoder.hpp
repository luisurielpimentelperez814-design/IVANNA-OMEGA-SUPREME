#pragma once

#include "SafModelLoader.hpp"
#include <cstddef>
#include <vector>

namespace Ivanna {

class SafPcaDecoder {
public:

    bool init(const SAFModel& model);

    std::vector<float> decode(
        const float* q,
        int dims
    ) const;

    // ── SAFMUS Nivel 6: decode sin asignacion por llamada ──────────────────
    // Longitud del vector decodificado (256 con el modelo horneado de 214
    // sujetos SOFA; p0_.size() con un modelo cargado). El llamador reserva
    // al menos esto y decodeInto() escribe sin asignar.
    std::size_t decodedLength() const noexcept;

    // Escribe el vector decodificado en `out` (capacidad outSize) y devuelve
    // cuantos valores escribio; 0 = error (q/out nulos o outSize == 0).
    // Misma matematica que decode(): p0 + sum_i V[i] * q[i], con el mismo
    // orden de acumulacion. El bucle interno esta vectorizado NEON en ARM
    // (guardia __ARM_NEON) con fallback escalar identico en host.
    std::size_t decodeInto(
        const float* q,
        int dims,
        float* out,
        std::size_t outSize
    ) const;

private:

    std::vector<float> p0_;
    std::vector<std::vector<float>> V_;

};

}
