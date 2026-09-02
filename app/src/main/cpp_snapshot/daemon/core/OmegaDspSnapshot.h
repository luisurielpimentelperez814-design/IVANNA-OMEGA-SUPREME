#pragma once
// OmegaDspSnapshot.h - Fuente de verdad atómica para parámetros DSP
// Arquitectura revolucionaria: Control Plane + Data Plane
// Objetivo: Audio inmersivo/binaural/espacial de clase mundial

#include <cstdint>
#include <array>
#include <cstring>

namespace omega {

struct OmegaDspSnapshot {
    // Identificación
    uint32_t magic;              // 0x4F4D4547 = "OMEG"
    uint32_t version;            // ABI version = 1
    uint64_t generation;         // Monotonic counter
    uint64_t timestamp_ms;       // Unix timestamp
    
    // Route profile
    enum class RouteProfile : uint8_t {
        OFF = 0,
        SYSTEM_WIDE = 1,
        IN_PROCESS = 2,
        PREVIEW = 3
    };
    RouteProfile route_profile;
    uint8_t padding[3];
    
    // Parámetros perceptuales
    float compressor;
    float exciter_reduction;
    float high_cut_hz;
    float spatial_width;
    float loudness_target;
    float harmonic_gain;
    float anti_dolby;
    float intensity;
    
    // Parámetros adaptativos
    float target_gain;
    float comp_amount;
    float exc_red;
    
    // EQ (10 bandas)
    std::array<float, 10> eq_gains;
    
    // PF (13 parámetros)
    std::array<float, 13> pf_params;
    
    // Estado perceptual (IA/ML)
    float speech_score;
    float music_score;
    uint8_t class_id;
    float confidence;
    
    // Estado SAF
    float saf_delta_energy;
    float saf_metric_norm;
    float saf_memory;
    float saf_gain;
    
    // Calibración ISO 226
    float listen_phon;
    float ref_phon;
    
    // Reserved
    std::array<float, 16> reserved;
    
    // Checksum
    uint32_t crc32;
    
    OmegaDspSnapshot() : 
        magic(0x4F4D4547), version(1), generation(0), timestamp_ms(0),
        route_profile(RouteProfile::OFF),
        compressor(0.5f), exciter_reduction(0.0f), high_cut_hz(20000.0f),
        spatial_width(1.0f), loudness_target(-14.0f), harmonic_gain(0.0f),
        anti_dolby(0.0f), intensity(0.5f), target_gain(0.0f), comp_amount(0.5f),
        exc_red(0.0f), speech_score(0.0f), music_score(0.0f), class_id(0),
        confidence(0.0f), saf_delta_energy(0.0f), saf_metric_norm(0.0f),
        saf_memory(0.0f), saf_gain(0.0f), listen_phon(60.0f), ref_phon(80.0f),
        crc32(0) {
        padding[0] = padding[1] = padding[2] = 0;
        eq_gains.fill(0.0f);
        pf_params.fill(0.0f);
        reserved.fill(0.0f);
    }
    
    uint32_t computeCRC32() const {
        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
        size_t len = sizeof(OmegaDspSnapshot) - sizeof(crc32);
        
        for (size_t i = 0; i < len; ++i) {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j) {
                crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
        }
        return crc ^ 0xFFFFFFFF;
    }
    
    bool isValid() const {
        return magic == 0x4F4D4547 && version == 1 && crc32 == computeCRC32();
    }
};

static_assert(sizeof(OmegaDspSnapshot) % 16 == 0, "Alineación 16 bytes");
static_assert(std::is_trivially_copyable<OmegaDspSnapshot>::value, "Trivially copyable");

} // namespace omega
