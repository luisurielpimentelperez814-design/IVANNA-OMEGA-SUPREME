#include "SafSpatialRuntime.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstddef>
#include <vector>
#include <array>

#include "spatial/ivanna_object_renderer.hpp"

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

    // ── Renderizador binaural de objetos (ruta de salida activa) ──
    ivanna::spatial::ObjectRenderer mRenderer;
    bool mSpatialActive = false;
    int  mBlockSize = 4096;
    std::vector<float> mObjBuf;      // [kUpmixObjects * 2 * numFrames]
    std::vector<float> mRenderL, mRenderR;
    std::vector<ivanna::spatial::AudioObject> mObjects;
    static constexpr int kUpmixObjects = 4;

public:
    IvannaFusionCore(float sampleRate = 48000.0f) : mSampleRate(sampleRate) {}

    void setSpatialWidth(float width) { mSpatialWidth = std::clamp(width, 0.1f, 3.0f); }
    void setHarmonicGain(float gain) { mHarmonicGain = std::clamp(gain, 0.0f, 2.0f); }
    void setCompressorParams(float thresholdDb, float ratio) {
        mCompressorThreshold = thresholdDb;
        mCompressorRatio = std::max(1.0f, ratio);
    }

    // Inicializa el renderer. blockSize debe ser >= el mayor numFrames esperado.
    void initSpatial(float sampleRate, int blockSize) {
        mSampleRate = sampleRate;
        mBlockSize  = blockSize;
        mRenderer.init(sampleRate, blockSize);
        mRenderer.setReverbLevel(0.20f);

        // Upmix perceptual: 4 objetos derivados del estéreo.
        // {id, x, y, z, width, gain, isBed, bedChannel, active}
        mObjects.resize(kUpmixObjects);
        mObjects[0] = {0,  0.0f,  1.0f, 0.0f, 0.2f, 0.65f, false, 0, true}; // central (frente)
        mObjects[1] = {1, -0.9f,  0.4f, 0.0f, 0.3f, 0.50f, false, 0, true}; // lateral izq
        mObjects[2] = {2,  0.9f,  0.4f, 0.0f, 0.3f, 0.50f, false, 0, true}; // lateral der
        mObjects[3] = {3,  0.0f, -1.0f, 0.0f, 0.6f, 0.35f, false, 0, true}; // ambiente (atrás)
        mRenderer.setObjects(mObjects);

        mRenderL.assign(blockSize, 0.f);
        mRenderR.assign(blockSize, 0.f);
        mObjBuf.assign((size_t)kUpmixObjects * 2 * blockSize, 0.f);
        mSpatialActive = true;
    }

    void setSpatialActive(bool active) { mSpatialActive = active; }
    bool isSpatialActive() const { return mSpatialActive; }

    // SAF adaptive spatial latent field [q0..q6]
    void setSafLatent(const std::array<float,7>& q)
    {
        mRenderer.setSafLatent(q);
    }

    // Carga un dataset HRTF personalizado (formato binario "IHR1").
    bool loadCustomHrtf(const char* path) {
        return mRenderer.loadHrtfDatasetFromFile(path);
    }

    // Propaga el vector latente q_t del optimizador Φ_SAF^∞ al renderer.
    // Llamar desde el hilo de control (SaFJniBridge) tras cada feedFeedback().
    void setSafLatentParams(const float q[7]) noexcept {
        mRenderer.setLatentParams(q);
    }

    void clearSafLatentParams() noexcept {
        mRenderer.clearLatentParams();
    }

    void processStereo(float* bufferLeft, float* bufferRight, size_t numFrames) {
        if (mSpatialActive && numFrames > 0 && numFrames <= (size_t)mBlockSize) {
            renderSpatial(bufferLeft, bufferRight, numFrames);
        }
        applyOutputDsp(bufferLeft, bufferRight, numFrames);
    }

private:
    void renderSpatial(float* bufferLeft, float* bufferRight, size_t numFrames) {
        const int N = (int)numFrames;
        const float w = mSpatialWidth;

        for (int n = 0; n < N; ++n) {
            const float L = bufferLeft[n];
            const float R = bufferRight[n];
            const float mid  = 0.5f * (L + R);
            const float side = 0.5f * (L - R);

            // Layout por objeto: [obj_L (N) | obj_R (N)]
            float* o0 = &mObjBuf[(size_t)0 * 2 * N];
            o0[n] = mid;            o0[N + n] = mid;             // central
            float* o1 = &mObjBuf[(size_t)1 * 2 * N];
            o1[n] = L * 0.5f * w;   o1[N + n] = L * 0.5f * w;    // lateral izq
            float* o2 = &mObjBuf[(size_t)2 * 2 * N];
            o2[n] = R * 0.5f * w;   o2[N + n] = R * 0.5f * w;    // lateral der
            float* o3 = &mObjBuf[(size_t)3 * 2 * N];
            o3[n] = side * 0.6f * w; o3[N + n] = side * 0.6f * w; // ambiente
        }

        // Ganancias dinámicas ligadas al width
        mObjects[1].gain = 0.50f * w;
        mObjects[2].gain = 0.50f * w;
        mObjects[3].gain = 0.35f * w;
        mRenderer.setObjects(mObjects);

        mRenderer.renderBlock(mObjBuf.data(), kUpmixObjects,
                              mRenderL.data(), mRenderR.data(), N);

        for (int n = 0; n < N; ++n) {
            bufferLeft[n]  = mRenderL[n];
            bufferRight[n] = mRenderR[n];
        }
    }

    void applyOutputDsp(float* bufferLeft, float* bufferRight, size_t numFrames) {
        const float alpha = 0.995f;
        for (size_t i = 0; i < numFrames; ++i) {
            float outL = bufferLeft[i];
            float outR = bufferRight[i];
            float satL = std::tanh(outL * mHarmonicGain);
            float satR = std::tanh(outR * mHarmonicGain);
            outL = 0.7f * outL + 0.3f * satL;
            outR = 0.7f * outR + 0.3f * satR;
            float maxPeak = std::max(std::abs(outL), std::abs(outR));
            mTargetGain = (maxPeak > 0.95f) ? (0.95f / maxPeak) : 1.0f;
            mCurrentGain = alpha * mCurrentGain + (1.0f - alpha) * mTargetGain;
            bufferLeft[i] = std::clamp(outL * mCurrentGain, -1.0f, 1.0f);
            bufferRight[i] = std::clamp(outR * mCurrentGain, -1.0f, 1.0f);
        }
    }
};
