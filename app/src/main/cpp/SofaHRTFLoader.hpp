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
        VALID_NOT_PARSED,   // Firma HDF5 OK. El archivo NO se parsea: las mediciones SOFA no
                            // se leen en runtime (usar tools/sofa_to_ihr1.py offline) y el
                            // HRIR sale de la base maestra SOFA-PCA (ver load()).
    };

    bool   load(const std::string& path);
    bool   loadCached(const std::string& path);
    Status lastStatus() const noexcept { return lastStatus_; }

    // true cuando el HRIR de hrtf() fue RECONSTRUIDO desde la base maestra SOFA-PCA y no leído
    // del archivo. load()==true solo prueba que el archivo empieza con la firma HDF5; el "sujeto"
    // se elige buscando su id en la RUTA, no en el contenido. Quien registre o muestre esta HRTF
    // no debe presentarla como una medición del archivo.
    bool isReconstructedFromMasterBasis() const noexcept {
        return lastStatus_ == Status::VALID_NOT_PARSED;
    }

    const HRTFIR& hrtf() const { return m_hrtf; }

private:
    HRTFIR m_hrtf;
    Status lastStatus_ = Status::NONE;
};

}
