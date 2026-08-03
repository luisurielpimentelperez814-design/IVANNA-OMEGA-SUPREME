#pragma once

#include <cmath>
#include <atomic>


struct SAFFullState
{
    std::atomic<float> Gt{1.0f};
    std::atomic<float> Mt{0.0f};

    std::atomic<float> deltaNorm{0.0f};
    std::atomic<float> deltaE{0.0f};

    std::atomic<float> gain{1.0f};
};


extern SAFFullState g_saf_full;



inline float projectionPiS(float x)
{
    // ΠS:
    // limita el estado al espacio estable

    if(x < 0.1f)
        return 0.1f;

    if(x > 2.0f)
        return 2.0f;

    return x;
}




inline float SAFFullUpdate(
        float current,
        float target)
{

    float delta =
        target - current;


    float G =
        g_saf_full.Gt.load();


    float norm =
        delta * G * delta;


    float previousMemory =
        g_saf_full.Mt.load();



    float memory =
        0.95f * previousMemory +
        0.05f * fabs(delta);



    float deltaE =
        fabs(target-current);



    float denominator =
        deltaE +
        norm +
        0.05f * memory +
        0.00001f;



    float step =
        deltaE /
        denominator;



    float update =
        current +
        step *
        (delta / G);



    float projected =
        projectionPiS(update);



    g_saf_full.Mt.store(memory);
    g_saf_full.deltaNorm.store(norm);
    g_saf_full.deltaE.store(deltaE);
    g_saf_full.gain.store(projected);



    return projected;
}

