#include "SofaHRTFLoader.hpp"
#include <fstream>
#include <cstdint>
#include <android/log.h>

#define LOG_TAG "IvannaSofaHRTF"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace Ivanna {

bool SofaHRTFLoader::load(const std::string& path) {
    LOGI("SofaHRTFLoader: Validando SOFA en %s", path.c_str());

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        lastStatus_ = Status::FILE_NOT_FOUND;
        LOGE("SofaHRTFLoader: Archivo no encontrado o sin permisos: %s", path.c_str());
        return false;
    }

    const std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // FIX: umbral mínimo reducido de 1024 → 512.
    // Un SOFA con pocos ángulos medidos puede tener cabecera de ~600 bytes.
    // El umbral anterior rechazaba archivos SOFA válidos pequeños como
    // archivos corruptos — silencioso, el fallback sintético quedaba activo
    // sin que nadie supiera que el archivo era válido pero demasiado pequeño
    // para el umbral.
    if (size < 512) {
        lastStatus_ = Status::CORRUPT;
        LOGE("SofaHRTFLoader: Archivo demasiado pequeño (%ld bytes) — "
             "corrompido o placeholder truncado.", (long)size);
        return false;
    }

    // Firma HDF5 completa (8 bytes): 0x89 H D F 0x0D 0x0A 0x1A 0x0A
    // El check anterior solo comparaba los primeros 4 bytes (0x89 H D F)
    // y dejaba pasar archivos binarios que empezaran con esa secuencia
    // pero no fueran HDF5 reales (ej. algunos formatos de audio propietarios).
    // FIX: comparar los 8 bytes completos de la firma.
    uint8_t magic[8] = {};
    if (!file.read(reinterpret_cast<char*>(magic), 8) || !file.good()) {
        lastStatus_ = Status::CORRUPT;
        LOGE("SofaHRTFLoader: Error leyendo cabecera de %s", path.c_str());
        return false;
    }

    const bool isHDF5 = (magic[0] == 0x89u && magic[1] == 'H'  &&
                         magic[2] == 'D'   && magic[3] == 'F'  &&
                         magic[4] == 0x0Du && magic[5] == 0x0Au &&
                         magic[6] == 0x1Au && magic[7] == 0x0Au);

    if (!isHDF5) {
        lastStatus_ = Status::NOT_HDF5;
        // Distinguir "no es SOFA" de "archivo corrompido" en el log para que
        // el desarrollador sepa exactamente qué pasó sin inspeccionar el archivo.
        LOGE("SofaHRTFLoader: %s NO es un SOFA AES69 válido — "
             "firma HDF5 incorrecta (esperado 89 48 44 46 0D 0A 1A 0A, "
             "encontrado %02X %02X %02X %02X %02X %02X %02X %02X).",
             path.c_str(),
             magic[0], magic[1], magic[2], magic[3],
             magic[4], magic[5], magic[6], magic[7]);
        return false;
    }

    // Archivo SOFA AES69 válido confirmado.
    // Los .sofa NO se parsean en runtime — la conversión AES69 → IHR1 ocurre
    // offline con tools/sofa_to_ihr1.py. Este loader solo valida integridad:
    // devolver true con Dirac reportaría "HRTF medido" cuando el pipeline usa
    // el sintético — silenciosamente destructivo para la toma de decisiones.
    // Status::VALID_NOT_PARSED es el resultado NORMAL y esperado; el caller
    // puede distinguirlo de los errores de archivo anteriores.
    lastStatus_ = Status::VALID_NOT_PARSED;
    m_hrtf.sampleRate = 48000.0f;
    m_hrtf.left.clear();
    m_hrtf.right.clear();

    LOGI("SofaHRTFLoader: SOFA AES69 válido (%ld bytes, firma HDF5 OK). "
         "Runtime usa IHR1 precalculado. Convertir con: tools/sofa_to_ihr1.py %s",
         (long)size, path.c_str());
    return false;
}

} // namespace Ivanna
