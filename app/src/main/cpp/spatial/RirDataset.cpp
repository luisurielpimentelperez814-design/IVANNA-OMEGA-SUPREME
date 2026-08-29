// spatial/RirDataset.cpp
#include "RirDataset.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <sstream>
#include <fstream>
#include <limits>
#include <algorithm>

namespace Ivanna {

namespace {

std::string trimCr(const std::string& s) {
    if (!s.empty() && s.back() == '\r') return s.substr(0, s.size() - 1);
    return s;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::stringstream ss(trimCr(line));
    std::string field;
    while (std::getline(ss, field, ',')) out.push_back(field);
    return out;
}

float toFloatSafe(const std::string& s, float fallback = 0.f) {
    // FIX: std::stof lanza std::invalid_argument / std::out_of_range.
    // El NDK compila con -fno-exceptions → try/catch ilegal en este target.
    // strtof devuelve 0.f en error y no lanza nada — equivalente seguro.
    if (s.empty()) return fallback;
    char* end = nullptr;
    float v = std::strtof(s.c_str(), &end);
    return (end != s.c_str()) ? v : fallback;
}

// ── Parser WAV PCM16 manual (RIFF/fmt /data), sin libsndfile ────────────────
struct WavRaw {
    bool ok = false;
    int channels = 0;
    int sampleRate = 0;
    int bitsPerSample = 0;
    std::vector<int16_t> samples; // interleaved
};

WavRaw readWavPcm16(const std::string& path) {
    WavRaw w;
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return w;

    char riff[4]; f.read(riff, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0) return w;
    f.seekg(4, std::ios::cur); // tamaño total del RIFF, no lo necesitamos
    char wave[4]; f.read(wave, 4);
    if (std::memcmp(wave, "WAVE", 4) != 0) return w;

    bool haveFmt = false, haveData = false;
    uint32_t dataSize = 0;
    std::streampos dataPos{};

    // Recorrer chunks hasta encontrar 'fmt ' y 'data' — algunos WAV traen
    // chunks extra (LIST/INFO) entre medio, no asumimos orden fijo.
    while (f.good() && !(haveFmt && haveData)) {
        char id[4];
        f.read(id, 4);
        if (!f.good()) break;
        uint32_t chunkSize = 0;
        f.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (!f.good()) break;

        if (std::memcmp(id, "fmt ", 4) == 0) {
            uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
            uint32_t sampleRate = 0;
            auto start = f.tellg();
            f.read(reinterpret_cast<char*>(&audioFormat), 2);
            f.read(reinterpret_cast<char*>(&numChannels), 2);
            f.read(reinterpret_cast<char*>(&sampleRate), 4);
            f.seekg(6, std::ios::cur); // byteRate(4) + blockAlign(2)
            f.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            // audioFormat==1 -> PCM entero. 3 -> IEEE float (no soportado aquí,
            // el dataset shippeado es PCM16 verificado con Python wave module).
            if (audioFormat != 1 || bitsPerSample != 16) {
                return w; // formato inesperado — no forzar, reportar fallo limpio
            }
            w.channels = numChannels;
            w.sampleRate = static_cast<int>(sampleRate);
            w.bitsPerSample = bitsPerSample;
            haveFmt = true;
            f.seekg(start);
            f.seekg(chunkSize, std::ios::cur);
        } else if (std::memcmp(id, "data", 4) == 0) {
            dataSize = chunkSize;
            dataPos = f.tellg();
            haveData = true;
            f.seekg(chunkSize, std::ios::cur);
        } else {
            f.seekg(chunkSize, std::ios::cur);
        }
        if (chunkSize % 2 == 1) f.seekg(1, std::ios::cur); // padding a word-align
    }

    if (!haveFmt || !haveData || w.channels <= 0) return w;

    f.clear();
    f.seekg(dataPos);
    size_t numSamples = dataSize / sizeof(int16_t);
    w.samples.resize(numSamples);
    f.read(reinterpret_cast<char*>(w.samples.data()), static_cast<std::streamsize>(dataSize));
    if (!f.good() && !f.eof()) return w;

    w.ok = true;
    return w;
}

} // namespace

bool RirDataset::load(const std::string& dir) {
    rooms_.clear();
    warnings_.clear();
    dir_ = dir;

    const std::string csvPath = dir_ + "/metadata.csv";
    std::ifstream f(csvPath);
    if (!f.good()) {
        warnings_.push_back("No se pudo abrir metadata.csv en " + csvPath);
        return false;
    }

    std::string header;
    if (!std::getline(f, header)) {
        warnings_.push_back("metadata.csv vacío");
        return false;
    }
    // Header esperado (verificado contra el CSV real shippeado):
    // filename,room_width_m,room_height_m,room_depth_m,src_x_m,src_y_m,
    // src_z_m,mic_x_m,mic_y_m,mic_z_m,distance_m,RT60_s
    // No validamos el header campo a campo — el orden es fijo y conocido del
    // dataset shippeado; si cambia, las filas se leerán con valores erróneos
    // pero sin crashear (toFloatSafe degrada a 0.f en vez de excepción).

    std::string line;
    size_t lineNo = 1;
    while (std::getline(f, line)) {
        ++lineNo;
        if (line.empty()) continue;
        auto fields = splitCsvLine(line);
        if (fields.size() < 12) {
            warnings_.push_back("metadata.csv línea " + std::to_string(lineNo) +
                                 ": columnas insuficientes (" + std::to_string(fields.size()) +
                                 "/12) — fila descartada");
            continue;
        }

        RirRoomMeta m;
        m.filename    = fields[0];
        m.roomWidthM  = toFloatSafe(fields[1]);
        m.roomHeightM = toFloatSafe(fields[2]);
        m.roomDepthM  = toFloatSafe(fields[3]);
        m.srcXM       = toFloatSafe(fields[4]);
        m.srcYM       = toFloatSafe(fields[5]);
        m.srcZM       = toFloatSafe(fields[6]);
        m.micXM       = toFloatSafe(fields[7]);
        m.micYM       = toFloatSafe(fields[8]);
        m.micZM       = toFloatSafe(fields[9]);
        m.distanceM   = toFloatSafe(fields[10]);
        m.rt60S       = toFloatSafe(fields[11]);

        // Verificar que el .wav referenciado exista de verdad en disco antes
        // de indexarlo — no prometer una sala que no está.
        std::ifstream wavCheck(dir_ + "/" + m.filename, std::ios::binary);
        if (!wavCheck.good()) {
            warnings_.push_back("metadata.csv línea " + std::to_string(lineNo) +
                                 ": " + m.filename + " no existe en " + dir_ + " — fila descartada");
            continue;
        }

        rooms_.push_back(std::move(m));
    }

    return !rooms_.empty();
}

size_t RirDataset::findNearestByRT60(float targetRt60S) const {
    size_t best = 0;
    float bestDiff = std::numeric_limits<float>::max();
    for (size_t i = 0; i < rooms_.size(); ++i) {
        float diff = std::fabs(rooms_[i].rt60S - targetRt60S);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
    }
    return best;
}

size_t RirDataset::findNearestByVolume(float targetVolumeM3) const {
    size_t best = 0;
    float bestDiff = std::numeric_limits<float>::max();
    for (size_t i = 0; i < rooms_.size(); ++i) {
        float diff = std::fabs(rooms_[i].volumeM3() - targetVolumeM3);
        if (diff < bestDiff) { bestDiff = diff; best = i; }
    }
    return best;
}

size_t RirDataset::findNearestSmart(float targetRt60S,
                                    float targetVolumeM3,
                                    float targetDistanceM) const {
    if (rooms_.empty()) return 0;
    if (rooms_.size() == 1) return 0;

    // Rangos reales del dataset (min-max) para normalizar cada criterio —
    // sin esto, comparar RT60 (≈0.3..2.5 s) contra distancia (≈0.6..9.4 m)
    // y volumen (≈50..800 m³) en bruto deja un solo criterio dominando.
    float rt60Min = 1e30f, rt60Max = -1e30f;
    float volMin  = 1e30f, volMax  = -1e30f;
    float distMin = 1e30f, distMax = -1e30f;
    for (const auto& r : rooms_) {
        rt60Min = std::min(rt60Min, r.rt60S);      rt60Max = std::max(rt60Max, r.rt60S);
        const float v = r.volumeM3();
        volMin  = std::min(volMin,  v);            volMax  = std::max(volMax,  v);
        distMin = std::min(distMin, r.distanceM);  distMax = std::max(distMax, r.distanceM);
    }
    const float rt60Span = (rt60Max - rt60Min) > 1e-6f ? (rt60Max - rt60Min) : 1.f;
    const float volSpan  = (volMax  - volMin)  > 1e-6f ? (volMax  - volMin)  : 1.f;
    const float distSpan = (distMax - distMin) > 1e-6f ? (distMax - distMin) : 1.f;

    // targetVolumeM3 <= 0 → objetivo neutro = volumen de la sala con el RT60
    // pedido más cercano (la geometría acompaña al RT60 en vez de inventarse).
    float volTarget = targetVolumeM3;
    if (volTarget <= 0.f) {
        volTarget = rooms_[findNearestByRT60(targetRt60S)].volumeM3();
    }
    // targetDistanceM <= 0 → mediana de distancias (sala ni íntima ni cavernosa).
    float distTarget = targetDistanceM;
    if (distTarget <= 0.f) {
        std::vector<float> dists;
        dists.reserve(rooms_.size());
        for (const auto& r : rooms_) dists.push_back(r.distanceM);
        std::sort(dists.begin(), dists.end());
        distTarget = dists[dists.size() / 2];
    }

    // Pesos: RT60 manda (es lo que se percibe como tamaño de sala), la
    // geometría desempata, la distancia refina (más cerca = más directo).
    constexpr float W_RT60 = 0.60f;
    constexpr float W_VOL  = 0.25f;
    constexpr float W_DIST = 0.15f;

    size_t best = 0;
    float bestScore = std::numeric_limits<float>::max();
    for (size_t i = 0; i < rooms_.size(); ++i) {
        const auto& r = rooms_[i];
        const float dRt60 = std::fabs(r.rt60S      - targetRt60S) / rt60Span;
        const float dVol  = std::fabs(r.volumeM3() - volTarget)   / volSpan;
        const float dDist = std::fabs(r.distanceM  - distTarget)  / distSpan;
        const float score = W_RT60 * dRt60 + W_VOL * dVol + W_DIST * dDist;
        if (score < bestScore) { bestScore = score; best = i; }
    }
    return best;
}

bool RirDataset::loadImpulseResponse(size_t idx, std::vector<float>& outL,
                                      std::vector<float>& outR, int& outSampleRate) const {
    outL.clear();
    outR.clear();
    outSampleRate = 0;
    if (idx >= rooms_.size()) return false;

    const std::string path = dir_ + "/" + rooms_[idx].filename;
    WavRaw w = readWavPcm16(path);
    if (!w.ok) return false;

    outSampleRate = w.sampleRate;
    constexpr float kInv32768 = 1.0f / 32768.0f;

    if (w.channels == 1) {
        outL.reserve(w.samples.size());
        outR.reserve(w.samples.size());
        for (int16_t s : w.samples) {
            float v = static_cast<float>(s) * kInv32768;
            outL.push_back(v);
            outR.push_back(v);
        }
    } else {
        // Estéreo (formato real del dataset shippeado) — tomar los 2
        // primeros canales si hubiera más (no esperado, pero defensivo).
        size_t frames = w.samples.size() / static_cast<size_t>(w.channels);
        outL.reserve(frames);
        outR.reserve(frames);
        for (size_t i = 0; i < frames; ++i) {
            outL.push_back(static_cast<float>(w.samples[i * w.channels + 0]) * kInv32768);
            outR.push_back(static_cast<float>(w.samples[i * w.channels + 1]) * kInv32768);
        }
    }
    return true;
}

void RirDataset::resampleLinear(std::vector<float>& channel, int irSr,
                                int sessionSr) {
    // No-op: sin datos, SR inválida, o ya a la tasa de sesión.
    if (channel.empty() || irSr <= 0 || sessionSr <= 0 || irSr == sessionSr)
        return;

    const double ratio = (double)sessionSr / (double)irSr;   // p.ej. 3.0 (16k→48k)
    const size_t srcN = channel.size();
    const size_t dstN = (size_t)((double)srcN * ratio + 0.5);
    if (dstN < 2) return;

    std::vector<float> out(dstN);
    const double step = (double)(srcN - 1) / (double)(dstN - 1);
    for (size_t i = 0; i < dstN; ++i) {
        const double pos = (double)i * step;
        const size_t i0 = (size_t)pos;
        const size_t i1 = (i0 + 1 < srcN) ? i0 + 1 : i0;
        const float frac = (float)(pos - (double)i0);
        out[i] = channel[i0] + frac * (channel[i1] - channel[i0]);
    }
    channel.swap(out);
}

} // namespace Ivanna
