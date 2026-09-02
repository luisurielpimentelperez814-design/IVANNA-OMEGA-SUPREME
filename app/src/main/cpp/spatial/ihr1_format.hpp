// ihr1_format.hpp — lector unico del contenedor HRIR "IHR1".
//
// POR QUE EXISTE ESTE ARCHIVO
// ---------------------------
// El arbol tenia TRES parsers independientes del mismo magic "IHR1", y no
// coincidian entre si:
//
//   Layout AZ  (escrito por tools/hrtf/sofa_to_ihr1.py)
//     magic[4] "IHR1" | i32 numPos | i32 irLen | i32 sampleRateHz
//     por posicion: [f32 az][f32 L x irLen][f32 R x irLen]
//     Lector: spatial/synthetic_hrtf.hpp (loadDatasetFromFile).
//
//   Layout AZEL (escrito por tools/sofa_to_ihr1.py)
//     magic[4] "IHR1" | u32 numPos | u32 irLen | u32 sampleRateHz
//     tabla:  [f32 az][f32 el] x numPos
//     luego:  [f32 L x irLen][f32 R x irLen] x numPos
//     Lectores: HRTFBinLoader y ObjectRenderer.
//
// Alimentar un lector con el fichero del OTRO escritor no falla: el magic
// coincide y la cabecera es identica, asi que los HRIR se leen corridos por
// el tamano de la tabla angular y el motor convoluciona datos desalineados.
// Es silencioso, es audible (imagen rota, peine, ITD sin sentido) y es muy
// dificil de atribuir desde el sintoma.
//
// Aqui hay UN lector: decide el layout por tamano de fichero (los dos
// tamanos son cerrados y distintos para la misma cabecera), rellena
// elevacion 0 cuando el layout no la trae, valida rangos con margen para
// datasets densos (CIPIC 1250, freefield 2354) y RECHAZA lecturas parciales
// en vez de publicar HRIR a medio rellenar.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ivanna {
namespace ihr1 {

enum class Layout { None, Az, AzEl };

struct Dataset {
    int32_t irLen        = 0;
    int32_t sampleRateHz = 0;
    Layout  layout       = Layout::None;
    std::vector<float> az;   // grados, [numPos]
    std::vector<float> el;   // grados, [numPos] (0 si el layout no la trae)
    std::vector<float> L;    // [numPos * irLen], contiguo por posicion
    std::vector<float> R;    // [numPos * irLen]

    int32_t numPositions() const {
        return irLen > 0 ? static_cast<int32_t>(L.size() / static_cast<size_t>(irLen)) : 0;
    }
    bool valid() const { return layout != Layout::None && irLen > 0 && numPositions() > 0; }
};

// Tamano exacto del fichero en cada layout — es lo que los distingue.
inline long expectedSizeAz(int64_t numPos, int64_t irLen) {
    return static_cast<long>(16 + numPos * (4 + 2 * irLen * 4));
}
inline long expectedSizeAzEl(int64_t numPos, int64_t irLen) {
    return static_cast<long>(16 + numPos * (8 + 2 * irLen * 4));
}

// Lee un IHR1 de cualquiera de los dos layouts. Devuelve false si el fichero
// no existe, el magic no coincide, la cabecera esta fuera de rango, el
// tamano no encaja con ningun layout o la lectura queda incompleta.
inline bool read(const char* path, Dataset& out) {
    if (!path) return false;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;

    char magic[4] = {};
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "IHR1", 4) != 0) {
        std::fclose(f);
        return false;
    }
    int32_t numPos = 0, irLen = 0, srHz = 0;
    if (std::fread(&numPos, 4, 1, f) != 1 ||
        std::fread(&irLen,  4, 1, f) != 1 ||
        std::fread(&srHz,   4, 1, f) != 1) {
        std::fclose(f);
        return false;
    }
    // Margen amplio (CIPIC 1250, freefield 2354) pero acotado: una cabecera
    // corrupta no debe poder reservar gigabytes.
    if (numPos <= 0 || numPos > 8192 || irLen <= 0 || irLen > 8192) {
        std::fclose(f);
        return false;
    }

    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return false; }
    const long fileSize = std::ftell(f);

    Layout layout = Layout::None;
    if (fileSize == expectedSizeAz(numPos, irLen))        layout = Layout::Az;
    else if (fileSize == expectedSizeAzEl(numPos, irLen)) layout = Layout::AzEl;
    if (layout == Layout::None) { std::fclose(f); return false; }

    if (std::fseek(f, 16, SEEK_SET) != 0) { std::fclose(f); return false; }

    out.irLen        = irLen;
    out.sampleRateHz = srHz;
    out.layout       = layout;
    out.az.assign(static_cast<size_t>(numPos), 0.f);
    out.el.assign(static_cast<size_t>(numPos), 0.f);
    out.L.assign(static_cast<size_t>(numPos) * static_cast<size_t>(irLen), 0.f);
    out.R.assign(static_cast<size_t>(numPos) * static_cast<size_t>(irLen), 0.f);

    const size_t taps = static_cast<size_t>(irLen);

    if (layout == Layout::AzEl) {
        // Tabla angular completa primero, HRIR despues.
        for (int32_t i = 0; i < numPos; ++i) {
            if (std::fread(&out.az[i], 4, 1, f) != 1 ||
                std::fread(&out.el[i], 4, 1, f) != 1) { std::fclose(f); return false; }
        }
        for (int32_t i = 0; i < numPos; ++i) {
            const size_t off = static_cast<size_t>(i) * taps;
            if (std::fread(&out.L[off], 4, taps, f) != taps ||
                std::fread(&out.R[off], 4, taps, f) != taps) { std::fclose(f); return false; }
        }
    } else {
        // Azimut y par de HRIR intercalados por posicion.
        for (int32_t i = 0; i < numPos; ++i) {
            const size_t off = static_cast<size_t>(i) * taps;
            if (std::fread(&out.az[i], 4, 1, f) != 1 ||
                std::fread(&out.L[off], 4, taps, f) != taps ||
                std::fread(&out.R[off], 4, taps, f) != taps) { std::fclose(f); return false; }
        }
    }

    std::fclose(f);
    return out.valid();
}

} // namespace ihr1
} // namespace ivanna
