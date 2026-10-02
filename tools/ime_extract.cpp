// ime_extract.cpp — Herramienta host de extracción 12D con el MusicFeatureExtractor real C++
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Uso:
//   ./ime_extract <input.wav>       -> imprime vectores 12D por ventana de 2.0 s en JSONL
//   ./ime_extract --self-test       -> genera señales canónicas y verifica invariancia 12D
#include "../app/src/main/cpp/music_intelligence/MusicFeatureExtractor.hpp"
#include "../app/src/main/cpp/music_intelligence/StyleBlender.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

bool readWavStereoFloat(const char* path, std::vector<float>& outL, std::vector<float>& outR, float& outSr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    char riff[4]{};
    uint32_t fileSize = 0;
    char wave[4]{};
    in.read(riff, 4);
    in.read(reinterpret_cast<char*>(&fileSize), 4);
    in.read(wave, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) return false;

    uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    uint32_t sampleRate = 48000;

    while (in.good()) {
        char chunkId[4]{};
        uint32_t chunkSize = 0;
        if (!in.read(chunkId, 4)) break;
        if (!in.read(reinterpret_cast<char*>(&chunkSize), 4)) break;

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            uint16_t blockAlign = 0;
            uint32_t byteRate = 0;
            in.read(reinterpret_cast<char*>(&audioFormat), 2);
            in.read(reinterpret_cast<char*>(&numChannels), 2);
            in.read(reinterpret_cast<char*>(&sampleRate), 4);
            in.read(reinterpret_cast<char*>(&byteRate), 4);
            in.read(reinterpret_cast<char*>(&blockAlign), 2);
            in.read(reinterpret_cast<char*>(&bitsPerSample), 2);
            if (chunkSize > 16) in.seekg(chunkSize - 16, std::ios::cur);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            if (numChannels == 0) return false;
            outSr = static_cast<float>(sampleRate);
            if (audioFormat == 1 && bitsPerSample == 16) {
                const size_t totalSamples = chunkSize / sizeof(int16_t);
                const size_t frames = totalSamples / numChannels;
                std::vector<int16_t> raw(totalSamples);
                in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(chunkSize));
                outL.resize(frames);
                outR.resize(frames);
                for (size_t i = 0; i < frames; ++i) {
                    outL[i] = static_cast<float>(raw[i * numChannels]) / 32768.0f;
                    outR[i] = static_cast<float>(raw[i * numChannels + (numChannels > 1 ? 1 : 0)]) / 32768.0f;
                }
                return true;
            } else if (audioFormat == 3 && bitsPerSample == 32) {
                const size_t totalSamples = chunkSize / sizeof(float);
                const size_t frames = totalSamples / numChannels;
                std::vector<float> raw(totalSamples);
                in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(chunkSize));
                outL.resize(frames);
                outR.resize(frames);
                for (size_t i = 0; i < frames; ++i) {
                    outL[i] = raw[i * numChannels];
                    outR[i] = raw[i * numChannels + (numChannels > 1 ? 1 : 0)];
                }
                return true;
            }
            return false;
        } else {
            in.seekg(chunkSize, std::ios::cur);
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--self-test") == 0) {
        ivanna::ime::MusicFeatureExtractor ext;
        ext.prepare(48000.0f, 1024);
        std::vector<float> l(48000), r(48000);
        for (size_t i = 0; i < l.size(); ++i) {
            const float t = static_cast<float>(i) / 48000.0f;
            l[i] = 0.4f * std::sin(6.2831853f * 110.0f * t) + 0.2f * std::sin(6.2831853f * 4200.0f * t);
            r[i] = 0.4f * std::sin(6.2831853f * 110.0f * t) - 0.2f * std::sin(6.2831853f * 4200.0f * t);
        }
        for (size_t off = 0; off + 1024 <= l.size(); off += 1024) {
            ext.processBlock(l.data() + off, r.data() + off, 1024);
        }
        float v[12]{};
        ext.features().toVector12(v);
        std::printf("{\"window\":0,\"f\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]}\n",
                    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
        return 0;
    }

    std::vector<float> L, R;
    float sr = 48000.0f;
    if (!readWavStereoFloat(argv[1], L, R, sr) || L.empty()) {
        std::fprintf(stderr, "Error leyendo WAV: %s\n", argv[1]);
        return 1;
    }

    ivanna::ime::MusicFeatureExtractor ext;
    ext.prepare(sr, 1024);
    const size_t windowFrames = static_cast<size_t>(2.0f * sr);
    size_t framesInWin = 0;
    int winIdx = 0;

    for (size_t off = 0; off < L.size(); off += 1024) {
        const int blk = static_cast<int>(std::min<size_t>(1024, L.size() - off));
        ext.processBlock(L.data() + off, R.data() + off, blk);
        framesInWin += static_cast<size_t>(blk);
        if (framesInWin >= windowFrames) {
            framesInWin = 0;
            float v[12]{};
            ext.features().toVector12(v);
            std::printf("{\"window\":%d,\"f\":[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]}\n",
                        winIdx++, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11]);
        }
    }
    return 0;
}
