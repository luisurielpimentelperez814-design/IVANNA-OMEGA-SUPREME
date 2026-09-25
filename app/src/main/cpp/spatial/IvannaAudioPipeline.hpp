#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include "StereoObjectDecomposer.hpp"
#include "HrtfPersonalizer.hpp"
#include "RoomProjectionEngine.hpp"
#include "ObjectSpatialRenderer.hpp"
#include "PhysicalSceneRenderer.hpp"
#include "HearingAdaptationEngine.hpp"
#include "../neuromorphic/CochlearActiveInverseModel.hpp"

namespace ivanna::spatial {

/**
 * @class IvannaAudioPipeline
 * Complete integration of all 6 spatial axes for Eje 7 Performance Certification.
 * 
 * Flow:
 * Input (Stereo L/R)
 *   -> StereoObjectDecomposer (Eje 1)
 *   -> ObjectSpatialRenderer (Eje 4, with HrtfPersonalizer cues Eje 2)
 *   -> PhysicalSceneRenderer (Eje 5)
 *   -> RoomProjectionEngine (Eje 3)
 *   -> HearingAdaptationEngine (Eje 6)
 *   -> Output (Stereo L/R)
 * 
 * Guarantee: Zero algorithmic added latency, zero heap allocations on hot path.
 */
class IvannaAudioPipeline {
public:
    static constexpr size_t MAX_BLOCK_SIZE = 512;

    static IvannaAudioPipeline& getActiveInstance() noexcept {
        static IvannaAudioPipeline s_instance;
        return s_instance;
    }

    IvannaAudioPipeline() noexcept {
        reset();
    }

    void reset() noexcept {
        decomposer_.reset();
        spatialRenderer_.reset();
        physicalScene_.reset();
        hearingEngine_.reset();
    }

    StereoObjectDecomposer& decomposer() noexcept { return decomposer_; }
    HrtfPersonalizer& personalizer() noexcept { return personalizer_; }
    RoomProjectionEngine& roomEngine() noexcept { return roomEngine_; }
    ObjectSpatialRenderer& spatialRenderer() noexcept { return spatialRenderer_; }
    PhysicalSceneRenderer& physicalScene() noexcept { return physicalScene_; }
    HearingAdaptationEngine& hearingEngine() noexcept { return hearingEngine_; }
    ivanna::neuromorphic::CochlearActiveInverseEngine& cochlearEngine() noexcept { return cochlearEngine_; }

    /**
     * @brief Renders an audio block through the complete 6-axis pipeline.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0 || numSamples > MAX_BLOCK_SIZE) return;

        // 1. Eje 1: Decompose stereo into 4 discrete objects
        float* objPtrs[4] = {
            objectBuffers_[0].data(),
            objectBuffers_[1].data(),
            objectBuffers_[2].data(),
            objectBuffers_[3].data()
        };
        decomposer_.decompose(bufferL, bufferR, objPtrs, numSamples);

        // 2. Eje 2 & 4: Spatial render 4 objects to stereo binaural stage
        // itdScale ahora sí se lee de HrtfPersonalizer (antes getItdScale()
        // no tenia caller — auditoria 2026-09-24).
        spatialRenderer_.renderObjects(objPtrs, decomposer_.getObjects(), bufferL, bufferR,
                                        numSamples, personalizer_.getItdScale());

        // 3. Eje 2: Apply personalized pinna/canal filter
        personalizer_.processChannel(bufferL, numSamples);
        personalizer_.processChannel(bufferR, numSamples);

        // 4. Eje 5: Physical scene occlusion and acoustic absorption
        physicalScene_.process(bufferL, bufferR, numSamples);

        // 5. Eje 3: Room partial inversion and virtual room projection
        roomEngine_.process(bufferL, bufferR, numSamples);

        // 6. Eje 6: Hearing adaptation & fatigue protection
        hearingEngine_.process(bufferL, bufferR, numSamples);
        // Eje Supremo Neuroacústico: inversión coclear activa (in-place, 0 ms)
        cochlearEngine_.process(bufferL, bufferR, numSamples);
    }

private:
    StereoObjectDecomposer decomposer_;
    HrtfPersonalizer personalizer_;
    RoomProjectionEngine roomEngine_;
    ObjectSpatialRenderer spatialRenderer_;
    PhysicalSceneRenderer physicalScene_;
    HearingAdaptationEngine hearingEngine_;
    ivanna::neuromorphic::CochlearActiveInverseEngine cochlearEngine_;

    // Static scratch memory for zero-allocation hot-path guarantee
    alignas(16) std::array<std::array<float, MAX_BLOCK_SIZE>, 4> objectBuffers_{};
};

} // namespace ivanna::spatial
