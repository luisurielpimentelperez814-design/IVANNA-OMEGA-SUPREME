#pragma once

#include <atomic>

struct SAFRuntimeState {

    std::atomic<float> gain {
        1.0f
    };

    std::atomic<float> compressor {
        0.0f
    };

    std::atomic<float> exciter {
        0.0f
    };

    std::atomic<float> spatial {
        1.0f
    };
};


extern SAFRuntimeState g_saf_state;
