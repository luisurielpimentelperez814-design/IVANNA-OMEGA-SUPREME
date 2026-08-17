#include "SafHRTFDatasetBridge.hpp"

#include <vector>
#include <cmath>

namespace Ivanna {


bool SafHRTFDatasetBridge::load(
    ivanna::SyntheticHRTF& hrtf,
    const char* path,
    uint32_t sampleRate
)
{
    HRTFBinLoader loader;

    if (!loader.load(path)) {
        printf("BRIDGE: loader FAILED path=%s\n", path);
        return false;
    }

    printf("BRIDGE: loader OK\n");


    const auto& header = loader.header();


    if (header.positions == 0 ||
        header.taps == 0) {
        printf("BRIDGE: invalid header pos=%u taps=%u\n",
               header.positions,
               header.taps);
        return false;
    }

    printf(
    "BRIDGE: header pos=%u taps=%u rate=%.1f\n",
    header.positions,
    header.taps,
    header.sampleRate
);


    std::vector<float> azimuths;
    std::vector<float> left;
    std::vector<float> right;


    azimuths.resize(header.positions);
    left.resize(
        (size_t)header.positions * header.taps
    );
    right.resize(
        (size_t)header.positions * header.taps
    );


    for (uint32_t i = 0; i < header.positions; i++)
    {
        // SOFA viene ordenado por SourcePosition.
        // Primer eje = azimuth.
        // La conversión ya dejó posiciones consecutivas.

        float az =
            -180.0f +
            (360.0f *
            (float)i /
            (float)(header.positions-1));


        azimuths[i] = az;


        const auto& e =
            loader.entry(i);


        for(uint32_t k=0;k<header.taps;k++)
        {
            left[
                (size_t)i*header.taps+k
            ] = e.left[k];

            right[
                (size_t)i*header.taps+k
            ] = e.right[k];
        }
    }


    hrtf.init(
        sampleRate,
        header.taps
    );


    bool result = hrtf.loadDataset(
        azimuths.data(),
        left.data(),
        right.data(),
        header.positions,
        header.taps
    );

    printf(
        "BRIDGE: dirs=%u taps=%u result=%d\n",
        header.positions,
        header.taps,
        result
    );

    return result;
}


}
