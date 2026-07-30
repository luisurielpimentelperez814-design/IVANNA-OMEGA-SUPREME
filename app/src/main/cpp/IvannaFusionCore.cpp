#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstddef>

// FIX (build host/CI): <arm_neon.h> se incluía sin guarda, así que el job
// "DSP Native Tests (host, CTest)" — que compila para x86_64 con GCC —
// abortaba con "fatal error: arm_neon.h: No such file or directory".
// Este .cpp NO usa ningún intrínseco NEON (verificado: cero float32x4/
// vld1/vst1/vmul/vadd en todo el archivo), así que la guarda basta y no
// hace falta ninguna ruta escalar alternativa. Se replica el mismo patrón
// que ya usa IvannaFusionCore.hpp:6-11, para no divergir del header.
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

class IvannaFusionCore {
private:
    float mSampleRate = 48000.0f;
    float mSpatialWidth = 1.20f;
    float mHarmonicGain = 1.0f;
    float mCompressorThreshold = -12.0f;
    float mCompressorRatio = 3.0f;
    float mMaxLoudnessLuFS = -14.0f;

    float mCurrentGain = 1.0f;
    float mTargetGain = 1.0f;

public:
    IvannaFusionCore(float sampleRate = 48000.0f) : mSampleRate(sampleRate) {}

    void setSpatialWidth(float width) { mSpatialWidth = std::clamp(width, 0.1f, 3.0f); }
    void setHarmonicGain(float gain) { mHarmonicGain = std::clamp(gain, 0.0f, 2.0f); }
    void setCompressorParams(float thresholdDb, float ratio) {
        mCompressorThreshold = thresholdDb;
        mCompressorRatio = std::max(1.0f, ratio);
    }

    void processStereo(float* bufferLeft, float* bufferRight, size_t numFrames) {
        float alpha = 0.995f;

        for (size_t i = 0; i < numFrames; ++i) {
            float left = bufferLeft[i];
            float right = bufferRight[i];

            // 1. Mid/Side Spatial Matrixing
            float mid = 0.5f * (left + right);
            float side = 0.5f * (left - right) * mSpatialWidth;

            float outL = mid + side;
            float outR = mid - side;

            // 2. Multiband Harmonic Exciter
            float satL = std::tanh(outL * mHarmonicGain);
            float satR = std::tanh(outR * mHarmonicGain);

            outL = 0.7f * outL + 0.3f * satL;
            outR = 0.7f * outR + 0.3f * satR;

            // 3. Peak Limiter / Safety Buffer
            float maxPeak = std::max(std::abs(outL), std::abs(outR));
            if (maxPeak > 0.95f) {
                mTargetGain = 0.95f / maxPeak;
            } else {
                mTargetGain = 1.0f;
            }

            mCurrentGain = alpha * mCurrentGain + (1.0f - alpha) * mTargetGain;

            bufferLeft[i] = std::clamp(outL * mCurrentGain, -1.0f, 1.0f);
            bufferRight[i] = std::clamp(outR * mCurrentGain, -1.0f, 1.0f);
        }
    }
};
