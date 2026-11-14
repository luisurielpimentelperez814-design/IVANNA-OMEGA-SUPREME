/*
 * ============================================================================
 * volterra_kernel_package.hpp — validación de kernels Volterra H1/H2 (contenedor IVK1)
 *
 * Contraparte C++ de tools/volterra_h2/kernel_loader.py: MISMO contenedor y MISMAS
 * reglas mínimas (defensa en profundidad; el motor no confía en el productor).
 *
 * POR QUÉ EXISTE: VolterraH2Symmetric::updateKernels() no tiene consumidor de
 * producción y retornaba void (rechazo silencioso). Este módulo da un camino
 * validado, con código de estado por causa, y deja el motor en bypass ante error.
 * NO activa nada por defecto: el estado de producción sigue siendo identidad
 * porque NO existe ningún kernel medido del sistema Moto G85+DAC+Genezy.
 *
 * CONTENEDOR IVK1 (little-endian, idéntico a kernel_loader.py::build_payload)
 *   off size campo
 *     0    4 magic           "IVK1"
 *     4    2 format_version  u16 = 1
 *     6    2 kernel_length   u16 K
 *     8    4 sample_rate_hz  u32
 *    12    4 crc32           u32 CRC-32 IEEE sobre los bytes [24, fin)
 *                            (device_id + h1 + h2). LIMITACIÓN conocida del
 *                            formato: K, fs y dataset_id NO entran en el CRC;
 *                            se protegen con la comprobación de tamaño exacto y
 *                            la igualdad contra la política.
 *    16    8 dataset_id      ascii, relleno NUL: "DATASET0" público REW (AJENO,
 *                            se rechaza SIEMPRE), "SYNTH000" sintético (solo
 *                            pruebas), "GENEZY01" medido en el sistema real.
 *    24   16 device_id       ascii, relleno NUL (puede ocupar los 16 bytes)
 *    40 4*K  h1[K] float32
 *   ... 4*K(K+1)/2 h2 triangular superior por filas, idx(k,l)=k*K+l-k(k+1)/2
 *   Tamaño total exacto = 40 + 4*K + 4*K(K+1)/2.
 *
 * Header-only, sin asignaciones, sin excepciones, compatible -fno-exceptions.
 * ============================================================================
 */

#ifndef IVANNA_VOLTERRA_KERNEL_PACKAGE_HPP
#define IVANNA_VOLTERRA_KERNEL_PACKAGE_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

// cppcheck no define __BYTE_ORDER__ (analiza, no compila): el #error solo
// aplica a compiladores reales (v2.5.442, Fase 0 de tests-host).
#if !defined(__CPPCHECK__) && (!defined(__BYTE_ORDER__) || (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__))
#error "IVK1 requiere plataforma little-endian (arm64/x86_64)"
#endif

namespace ivanna {
namespace dsp {

enum class VolterraLoadStatus : uint8_t {
    Ok = 0,
    NullArgument,
    Truncated,
    BadMagic,
    BadVersion,          // format_version, header_size o model_version
    SizeMismatch,        // conteos incoherentes con K o bytes sobrantes
    CrcMismatch,
    BadString,           // device_id/provenance sin NUL final
    FlagsInconsistent,   // flags contradictorios con el payload
    ForeignDatasetRejected,
    SyntheticRejected,
    SampleRateMismatch,
    KernelLengthMismatch,
    ChannelMismatch,
    DeviceMismatch,
    NonFinite,
    AmplitudeLimit,
    UnstableH1,
    H2BudgetExceeded,
    H2TooLarge,
    EngineBusy,          // motor habilitado / no silencioso / callbacks activos
    EngineUnavailable,   // motor sin almacenamiento
    AllocFailed,
    UnknownDataset       // dataset_id no reconocido
};

inline const char* volterraLoadStatusName(VolterraLoadStatus s) noexcept {
    switch (s) {
        case VolterraLoadStatus::Ok: return "Ok";
        case VolterraLoadStatus::NullArgument: return "NullArgument";
        case VolterraLoadStatus::Truncated: return "Truncated";
        case VolterraLoadStatus::BadMagic: return "BadMagic";
        case VolterraLoadStatus::BadVersion: return "BadVersion";
        case VolterraLoadStatus::SizeMismatch: return "SizeMismatch";
        case VolterraLoadStatus::CrcMismatch: return "CrcMismatch";
        case VolterraLoadStatus::BadString: return "BadString";
        case VolterraLoadStatus::FlagsInconsistent: return "FlagsInconsistent";
        case VolterraLoadStatus::ForeignDatasetRejected: return "ForeignDatasetRejected";
        case VolterraLoadStatus::SyntheticRejected: return "SyntheticRejected";
        case VolterraLoadStatus::SampleRateMismatch: return "SampleRateMismatch";
        case VolterraLoadStatus::KernelLengthMismatch: return "KernelLengthMismatch";
        case VolterraLoadStatus::ChannelMismatch: return "ChannelMismatch";
        case VolterraLoadStatus::DeviceMismatch: return "DeviceMismatch";
        case VolterraLoadStatus::NonFinite: return "NonFinite";
        case VolterraLoadStatus::AmplitudeLimit: return "AmplitudeLimit";
        case VolterraLoadStatus::UnstableH1: return "UnstableH1";
        case VolterraLoadStatus::H2BudgetExceeded: return "H2BudgetExceeded";
        case VolterraLoadStatus::H2TooLarge: return "H2TooLarge";
        case VolterraLoadStatus::EngineBusy: return "EngineBusy";
        case VolterraLoadStatus::EngineUnavailable: return "EngineUnavailable";
        case VolterraLoadStatus::AllocFailed: return "AllocFailed";
        case VolterraLoadStatus::UnknownDataset: return "UnknownDataset";
    }
    return "Unknown";
}

namespace ivk1 {
constexpr char     kMagic[4]       = {'I', 'V', 'K', '1'};
constexpr uint16_t kFormatVersion  = 1;
constexpr size_t   kOffMagic       = 0;
constexpr size_t   kOffVersion     = 4;
constexpr size_t   kOffKernelLen   = 6;
constexpr size_t   kOffSampleRate  = 8;
constexpr size_t   kOffCrc         = 12;
constexpr size_t   kOffDatasetId   = 16;
constexpr size_t   kDatasetIdSize  = 8;
constexpr size_t   kOffDeviceId    = 24;
constexpr size_t   kDeviceIdSize   = 16;
constexpr size_t   kHeaderSize     = 40;
static_assert(kOffDatasetId + kDatasetIdSize == kOffDeviceId, "IVK1 layout");
static_assert(kOffDeviceId + kDeviceIdSize == kHeaderSize, "IVK1 layout");
static_assert(sizeof(float) == 4, "IVK1 requiere float32 IEEE-754");

constexpr char kDatasetPublicRew[9] = "DATASET0";   // dataset público: AJENO
constexpr char kDatasetSynthetic[9] = "SYNTH000";   // pruebas
constexpr char kDatasetGenezy[9]    = "GENEZY01";   // medición física del sistema real

inline uint32_t rdU32(const uint8_t* p) noexcept { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint16_t rdU16(const uint8_t* p) noexcept { uint16_t v; std::memcpy(&v, p, 2); return v; }
inline float    rdF32(const uint8_t* p) noexcept { float v;    std::memcpy(&v, p, 4); return v; }

/** Índice del triángulo superior por filas (l >= k), igual que el motor. */
constexpr uint64_t h2Index(uint64_t k, uint64_t l, uint64_t K) noexcept {
    return k * K + l - (k * (k + 1)) / 2;
}
} // namespace ivk1

/** CRC-32 IEEE (reflejado, poly 0xEDB88320) == zlib.crc32. */
inline uint32_t volterraCrc32(const uint8_t* data, size_t size) noexcept {
    struct Table {
        uint32_t t[256];
        constexpr Table() : t() {
            for (uint32_t i = 0; i < 256; ++i) {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                t[i] = c;
            }
        }
    };
    static constexpr Table table{};
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) crc = table.t[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/** Política de aceptación. Los valores por defecto son los de PRODUCCIÓN. */
struct VolterraLoadPolicy {
    uint32_t expected_sample_rate_hz = 48000;
    uint32_t expected_kernel_length  = 64;   // el motor lo sobrescribe con su K real
    // Dispositivo caracterizado que el paquete debe declarar (<=16 caracteres).
    char     expected_device_id[ivk1::kDeviceIdSize + 1] = "MOTO_G85_GENEZY";
    // Segundo dispositivo autorizado EXPLÍCITO (misma familia de medición):
    // la cadena refinada Moto G85 5G + DAC USB-C + Sony Genezi MHC-PZ1D
    // (v2.5.369). No es un comodín: solo estos dos dispositivos pasan la
    // puerta. Un alt vacío ("") no autoriza nada (ver validación).
    char     expected_device_id_alt[ivk1::kDeviceIdSize + 1] = "MOTO_G85_PZ1D";

    // Mismos límites que kernel_loader.py (suelo común) + cota de H2.
    float    max_abs_coeff           = 8.0f;        // |h1[k]|, |h2[i]|
    float    max_h1_l1               = 1.000001f;   // sum|h1|; como |H1(e^jw)| <= l1, también acota la ganancia
    float    max_h2_energy           = 64.0f;       // sum h2^2 (triangular, como el loader Python)
    float    input_amplitude         = 1.0f;        // amplitud de entrada supuesta (fondo de escala)
    float    h2_budget               = 0.25f;       // A^2 * sum|h2 completa|  (off-diagonal x2)
    float    max_worst_case_output   = 1.5f;        // A*sum|h1| + A^2*sum|h2|
    uint32_t max_kernel_length       = 1024;        // H2 es O(K^2) y IVK1 siempre lo transporta

    bool     allow_synthetic         = false;       // SÓLO pruebas (dataset SYNTH000)
    // DATASET0 (público) no tiene opción de permitir: se rechaza siempre.
};

/** Vista validada sobre los bytes del paquete (no copia). */
struct VolterraPackageView {
    uint32_t sample_rate_hz = 0;
    uint32_t kernel_length = 0;
    const uint8_t* h1_bytes = nullptr;   // 4*K bytes (posible no alineación)
    const uint8_t* h2_bytes = nullptr;   // 4*K(K+1)/2 bytes
};

namespace detail {
inline bool fieldEquals(const uint8_t* field, size_t n, const char* s) noexcept {
    size_t i = 0;
    for (; i < n && s[i] != 0; ++i) if (field[i] != static_cast<uint8_t>(s[i])) return false;
    if (i == n) return s[n] == 0;                        // la cadena esperada debe terminar aquí
    for (; i < n; ++i) if (field[i] != 0) return false;  // el resto del campo debe ser NUL
    return true;
}
} // namespace detail

/**
 * Valida un paquete IVK1. Hilo de CONTROL; no asigna. El primer fallo gana, de lo
 * estructural a lo físico, con un código distinto por causa.
 */
inline VolterraLoadStatus volterraValidatePackage(
    const uint8_t* data, size_t size, const VolterraLoadPolicy& pol,
    VolterraPackageView* out = nullptr) noexcept {
    using S = VolterraLoadStatus;
    if (!data) return S::NullArgument;
    if (size < ivk1::kHeaderSize) return S::Truncated;
    if (std::memcmp(data + ivk1::kOffMagic, ivk1::kMagic, 4) != 0) return S::BadMagic;
    if (ivk1::rdU16(data + ivk1::kOffVersion) != ivk1::kFormatVersion) return S::BadVersion;

    const uint32_t K  = ivk1::rdU16(data + ivk1::kOffKernelLen);
    const uint32_t fs = ivk1::rdU32(data + ivk1::kOffSampleRate);
    if (K == 0) return S::KernelLengthMismatch;
    const uint64_t tri = (static_cast<uint64_t>(K) * (K + 1u)) / 2u;
    const uint64_t expected = ivk1::kHeaderSize + 4ull * K + 4ull * tri;
    if (static_cast<uint64_t>(size) < expected) return S::Truncated;
    if (static_cast<uint64_t>(size) > expected) return S::SizeMismatch;

    const uint32_t crc = ivk1::rdU32(data + ivk1::kOffCrc);
    if (volterraCrc32(data + ivk1::kOffDeviceId, size - ivk1::kOffDeviceId) != crc) return S::CrcMismatch;

    // Procedencia: el dataset público es de OTRO sistema y jamás entra.
    const uint8_t* ds = data + ivk1::kOffDatasetId;
    if (detail::fieldEquals(ds, ivk1::kDatasetIdSize, ivk1::kDatasetPublicRew)) return S::ForeignDatasetRejected;
    if (detail::fieldEquals(ds, ivk1::kDatasetIdSize, ivk1::kDatasetSynthetic)) {
        if (!pol.allow_synthetic) return S::SyntheticRejected;
    } else if (!detail::fieldEquals(ds, ivk1::kDatasetIdSize, ivk1::kDatasetGenezy)) {
        return S::UnknownDataset;
    }

    if (fs != pol.expected_sample_rate_hz) return S::SampleRateMismatch;
    if (K != pol.expected_kernel_length) return S::KernelLengthMismatch;
    // Dispositivo: debe ser el principal o el alternativo EXPLÍCITO
    // (MOTO_G85_GENEZY o MOTO_G85_PZ1D). Un alt vacío no autoriza nada.
    const bool devOk =
        detail::fieldEquals(data + ivk1::kOffDeviceId, ivk1::kDeviceIdSize,
                            pol.expected_device_id) ||
        (pol.expected_device_id_alt[0] != '\0' &&
         detail::fieldEquals(data + ivk1::kOffDeviceId, ivk1::kDeviceIdSize,
                             pol.expected_device_id_alt));
    if (!devOk) return S::DeviceMismatch;
    if (K > pol.max_kernel_length) return S::H2TooLarge;

    const uint8_t* h1p = data + ivk1::kHeaderSize;
    const uint8_t* h2p = h1p + 4ull * K;
    double l1 = 0.0, h2Abs = 0.0, h2Energy = 0.0;
    float maxAbs = 0.0f;
    for (uint32_t k = 0; k < K; ++k) {
        const float v = ivk1::rdF32(h1p + 4ull * k);
        if (!std::isfinite(v)) return S::NonFinite;
        const float a = std::fabs(v);
        if (a > maxAbs) maxAbs = a;
        l1 += a;
    }
    for (uint32_t k = 0; k < K; ++k) {
        for (uint32_t l = k; l < K; ++l) {
            const float v = ivk1::rdF32(h2p + 4ull * ivk1::h2Index(k, l, K));
            if (!std::isfinite(v)) return S::NonFinite;
            const float a = std::fabs(v);
            if (a > maxAbs) maxAbs = a;
            h2Abs += (l == k ? 1.0 : 2.0) * static_cast<double>(a);
            h2Energy += static_cast<double>(v) * static_cast<double>(v);
        }
    }
    if (maxAbs > pol.max_abs_coeff) return S::AmplitudeLimit;
    if (l1 > pol.max_h1_l1) return S::UnstableH1;
    if (h2Energy > pol.max_h2_energy) return S::H2BudgetExceeded;
    const double A = static_cast<double>(pol.input_amplitude);
    if (A * A * h2Abs > pol.h2_budget) return S::H2BudgetExceeded;
    if (A * l1 + A * A * h2Abs > pol.max_worst_case_output) return S::AmplitudeLimit;

    if (out) {
        out->sample_rate_hz = fs;
        out->kernel_length = K;
        out->h1_bytes = h1p;
        out->h2_bytes = h2p;
    }
    return S::Ok;
}

} // namespace dsp
} // namespace ivanna

#endif // IVANNA_VOLTERRA_KERNEL_PACKAGE_HPP
