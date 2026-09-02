#pragma once

#include <string>
#include <vector>

namespace Ivanna {

struct HRTFIR {
    std::vector<float> left;
    std::vector<float> right;
    float sampleRate = 48000.0f;
};

class SofaHRTFLoader {
public:
    // FIX: antes load() solo devolvía bool — el caller no podía distinguir
    // "archivo no encontrado", "firma HDF5 incorrecta" y "SOFA válido pero
    // no parseado en runtime". Ahora lastStatus() da el diagnóstico exacto.
    enum class Status {
        NONE,               // No se ha llamado a load() aún
        FILE_NOT_FOUND,     // El path no existe o no hay permisos de lectura
        CORRUPT,            // Tamaño insuficiente o error de I/O en cabecera
        NOT_HDF5,           // El archivo existe pero no tiene firma HDF5 válida
        VALID_NOT_PARSED,   // Firma HDF5 OK — archivo SOFA válido, no se parsea
                            // en runtime (usar tools/sofa_to_ihr1.py offline)
    };

    bool   load(const std::string& path);
    Status lastStatus() const noexcept { return lastStatus_; }

    const HRTFIR& hrtf() const { return m_hrtf; }

private:
    HRTFIR m_hrtf;
    Status lastStatus_ = Status::NONE;
};

}

}
