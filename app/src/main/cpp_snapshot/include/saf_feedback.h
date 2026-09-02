#pragma once

#include <cmath>
#include <atomic>

struct SAFMetrics {

    std::atomic<float> outputEnergy{0.0f};
    std::atomic<float> targetEnergy{1.0f};
    std::atomic<float> errorEnergy{0.0f};
    std::atomic<float> deltaE{0.0f};

};


extern SAFMetrics g_saf_metrics;



inline void updateSAFFeedback(float sample)
{
    float energy = sample * sample;

    g_saf_metrics.outputEnergy.store(
        energy
    );


    float target =
        g_saf_metrics.targetEnergy.load();


    float error =
        target - energy;


    g_saf_metrics.errorEnergy.store(
        error
    );


    g_saf_metrics.deltaE.store(
        fabs(error)
    );
}



inline float calculateSAFGain()
{
    float e =
        g_saf_metrics.deltaE.load();


    float gain =
        e /
        (e + 0.00001f);


    if(gain < 0.1f)
        gain = 0.1f;


    if(gain > 2.0f)
        gain = 2.0f;


    return gain;
}

