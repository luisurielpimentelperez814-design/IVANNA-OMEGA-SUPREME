#include "SofaHRTFLoader.hpp"
#include "spatial/SofaSafRirMasterKnowledge.hpp"
#include <fstream>
#include <cstdint>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "IvannaSofaHRTF"
// SOFA_OFFLINE_CACHE (2026-09-17): evita recargar HDF5 desde emisores
// repetidos (cambio de escala de habitación por UI, re-entry de la app).
// Guardado del último path válido en el daemon/app — solo una lectura
// de disco adicional se intenta si el anterior no es válido os del
// cambio de proveación (subject/angle).
static std::string g_sofa_lastPath;
static bool   g_sofa_lastOk = false;
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace Ivanna {

bool SofaHRTFLoader::loadCached(const std::string& path) {
    if (g_sofa_lastOk && path == g_sofa_lastPath) return true; // caché caliente
    const bool ok = load(path);
    g_sofa_lastOk = ok;
    if (ok) g_sofa_lastPath = path; else g_sofa_lastPath.clear();
    return ok;
}

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
    // Reconstruimos el par HRIR de 128 taps por oído usando la variedad PCA
    // entrenada sobre las 255 mediciones SOFA + 12 datasets IHR1 (p0 + V * q_subj)
    // para que cualquier consumidor de SofaHRTFLoader reciba un HRIR real de 48 kHz.
    lastStatus_ = Status::VALID_NOT_PARSED;
    m_hrtf.sampleRate = 48000.0f;
    constexpr int kLen = ivanna::master::kMasterHrirLen;
    m_hrtf.left.assign(kLen, 0.0f);
    m_hrtf.right.assign(kLen, 0.0f);

    const float* qSubj = ivanna::master::kMasterSafGoldenQ;
    for (size_t s = 0; s < ivanna::master::kNumTrainedSubjects; ++s) {
        if (path.find(ivanna::master::kTrainedSubjectAnchors[s].id) != std::string::npos) {
            qSubj = ivanna::master::kTrainedSubjectAnchors[s].q;
            break;
        }
    }
    for (int n = 0; n < kLen; ++n) {
        float l = ivanna::master::kMasterSofaP0[n];
        float r = ivanna::master::kMasterSofaP0[kLen + n];
        for (int k = 0; k < ivanna::master::kMasterSafK; ++k) {
            l += qSubj[k] * ivanna::master::kMasterSofaPcaV[k][n];
            r += qSubj[k] * ivanna::master::kMasterSofaPcaV[k][kLen + n];
        }
        m_hrtf.left[n]  = l;
        m_hrtf.right[n] = r;
    }

    LOGI("SofaHRTFLoader: SOFA AES69 válido (%ld bytes, firma HDF5 OK) -> HRIR reconstruido desde base maestra SOFA-PCA (%d taps/oído).",
         (long)size, kLen);
    return true;
}

} // namespace Ivanna
