#include "SofaHRTFLoader.hpp"
#include <fstream>
#include <iostream>
#include <android/log.h>

#define LOG_TAG "IvannaSofaHRTF"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace Ivanna {

bool SofaHRTFLoader::load(const std::string& path) {
    LOGI("SofaHRTFLoader: Intentando cargar archivo SOFA en %s", path.c_str());

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        LOGE("SofaHRTFLoader: Error al abrir archivo - no existe o faltan permisos: %s", path.c_str());
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Validación de tamaño mínimo (un SOFA real tiene cabecera NetCDF/HDF5, típicamente > 1KB)
    if (size < 1024) {
        LOGE("SofaHRTFLoader: Archivo demasiado pequeño (%ld bytes) para ser un SOFA/HDF5 válido.", (long)size);
        return false;
    }

    // Leer primeros bytes para validar firma HDF5 (SOFA está basado en NetCDF4/HDF5)
    // HDF5 Magic Signature: \211 H D F \r \n \032 \n (0x89 0x48 0x44 0x46 0x0D 0x0A 0x1A 0x0A)
    char magic[8];
    if (file.read(magic, 8)) {
        bool isHDF5 = (magic[0] == (char)0x89 && magic[1] == 'H' && magic[2] == 'D' && magic[3] == 'F');
        // NOTA: Para compatibilidad futura con SAF_MODEL.json o hrtf_database.bin, 
        // no forzamos fallo si no es HDF5 puro, pero documentamos el estado.
        if (!isHDF5) {
            LOGI("SofaHRTFLoader: El archivo no tiene cabecera HDF5 pura, asumiendo formato legacy/custom bin.");
        }
    } else {
        LOGE("SofaHRTFLoader: Error leyendo cabecera del archivo.");
        return false;
    }

    // TODO: Integración real de libmysofa o NetCDF para extraer Data.IR
    // Por el momento, la carga pasa la validación física, poblamos con TEST DATA / PLACEHOLDER.

    // ==========================================
    // TEST DATA / PLACEHOLDER 
    // Mantiene compatibilidad con el pipeline DSP
    // ==========================================
    m_hrtf.sampleRate = 48000.0f;
    m_hrtf.left.assign(512, 0.0f);
    m_hrtf.right.assign(512, 0.0f);
    // Impulso en dirac para no destruir el audio
    m_hrtf.left[0] = 1.0f;
    m_hrtf.right[0] = 1.0f;
    
    LOGI("SofaHRTFLoader: Validacion completada (Tam: %ld). Placeholder impulsos cargados.", (long)size);
    return true;
}

}
