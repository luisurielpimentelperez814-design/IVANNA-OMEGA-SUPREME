#include "IvannaFusionCore.hpp"

extern "C" {

static Ivanna::IvannaFusionCore g_coreEngine;
static Ivanna::AudioBuffer g_scratchBuffer;

void IvannaOmega_ProcessAudio(float* leftChannel, float* rightChannel, int frameCount) {
    if (!leftChannel || !rightChannel || frameCount <= 0) return;

    int processed = 0;
    while (processed < frameCount) {
        int chunkSize = std::min(frameCount - processed, static_cast<int>(Ivanna::BLOCK_SIZE));

        for (int i = 0; i < chunkSize; ++i) {
            g_scratchBuffer.left[i] = leftChannel[processed + i];
            g_scratchBuffer.right[i] = rightChannel[processed + i];
        }

        g_coreEngine.processBlock(&g_scratchBuffer);

        for (int i = 0; i < chunkSize; ++i) {
            leftChannel[processed + i] = g_scratchBuffer.left[i];
            rightChannel[processed + i] = g_scratchBuffer.right[i];
        }

        processed += chunkSize;
    }
}

void IvannaOmega_UpdateParams(float gainDb, float compThresh, float compRatio, 
                              float exciteEven, float exciteOdd, float lowPassCutoff) {
    g_coreEngine.setParameters(gainDb, compThresh, compRatio, exciteEven, exciteOdd, lowPassCutoff);
}

}
