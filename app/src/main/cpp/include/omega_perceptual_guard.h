#pragma once

#include <algorithm>
#include <cmath>

class OmegaPerceptualGuard {

public:

    struct Limits {

        float gain = 1.0f;
        float compressor = 0.0f;
        float exciterReduction = 1.0f;
        float width = 1.0f;
    };


    Limits process(
        float lufs,
        float brightness,
        float crest
    )
    {

        Limits out;


        float loudnessPressure =
            std::clamp(
                (lufs + 14.0f) / 10.0f,
                0.0f,
                1.0f
            );


        out.gain =
            1.0f -
            loudnessPressure * 0.20f;



        float hf =
            std::clamp(
                (brightness - 0.60f) / 0.40f,
                0.0f,
                1.0f
            );


        out.exciterReduction =
            1.0f -
            (hf * 0.65f);



        out.width =
            1.0f -
            (hf * 0.25f);



        float crestProtection =
            std::clamp(
                (2.0f - crest) / 2.0f,
                0.0f,
                1.0f
            );


        out.compressor =
            crestProtection * 0.45f;


        return out;
    }

};

