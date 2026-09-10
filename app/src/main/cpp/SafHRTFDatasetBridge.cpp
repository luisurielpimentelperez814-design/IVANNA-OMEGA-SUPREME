#include "SafHRTFDatasetBridge.hpp"

#include <vector>
#include <cstring>
#include <cmath>
#include <cstdio>

// Log al logcat en dispositivo; printf en host (los tests host compilan este
// archivo y no tienen liblog). Antes era printf siempre: en Android stdout
// no va a ninguna parte visible — la telemetria del camino de carga del HRTF
// medido era invisible en dispositivo.
#ifdef __ANDROID__
#include <android/log.h>
#define BRIDGE_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "SafHRTFBridge", __VA_ARGS__)
#define BRIDGE_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "SafHRTFBridge", __VA_ARGS__)
#else
#define BRIDGE_LOGI(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#define BRIDGE_LOGW(...) do { std::printf(__VA_ARGS__); std::printf("\n"); } while (0)
#endif

namespace Ivanna {


bool SafHRTFDatasetBridge::load(
    ivanna::SyntheticHRTF& hrtf,
    const char* path,
    uint32_t sampleRate
)
{
    HRTFBinLoader loader;

    if (!loader.load(path)) {
        BRIDGE_LOGW("loader FAILED path=%s", path ? path : "(null)");
        return false;
    }


    const auto& header = loader.header();


    // Defensa en profundidad (el loader ya valida, pero este bridge es una
    // frontera publica: si alguien lo llama con otro loader, no NaN aqui).
    if (header.positions == 0 || header.taps == 0) {
        BRIDGE_LOGW("invalid header pos=%u taps=%u", header.positions, header.taps);
        return false;
    }

    BRIDGE_LOGI("header pos=%u taps=%u rate=%.1f fmt=%s",
                header.positions, header.taps, header.sampleRate,
                loader.isIHR1Format() ? "IHR1" : "IVHRTF01");


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
        const auto& e = loader.entry(i);

        // Azimut por posición:
        //   - IHR1: tabla az+el medida por posición (leída en loadIHR1,
        //     guardada en HRTFEntry.azimuthDeg) — es la posición REAL de
        //     cada medición, la que el convolver necesita para interpolar.
        //   - IVHRTF01: no trae tabla de ángulos (formato legacy, solo
        //     HRIRs consecutivas) — fallback: rejilla uniforme
        //     -180..+180 como aproximación documentada.
        // FIX (division por cero): con un dataset legal de UNA sola posicion
        // (positions==1 esta dentro del rango que el loader acepta), el
        // fallback legacy hacia 360*i/(1-1) = i/0 -> azimut NaN -> el
        // convolver interpolaba con NaN y el HRTF medido moria en silencio.
        // Con una posicion la unica respuesta honesta es azimut 0 (frente).
        float az;
        if (loader.isIHR1Format()) {
            az = e.azimuthDeg;
        } else if (header.positions > 1) {
            az = -180.0f + (360.0f * (float)i / (float)(header.positions - 1));
        } else {
            az = 0.0f;
        }

        azimuths[i] = az;

        // memcpy por fila: e.left/e.right son vectores contiguos. Antes era
        // un bucle float a float — 727,040 copias individuales (710x512x2)
        // en el arranque del efecto, puro desperdicio medible.
        std::memcpy(left.data()  + (size_t)i * header.taps,
                    e.left.data(),  (size_t)header.taps * sizeof(float));
        std::memcpy(right.data() + (size_t)i * header.taps,
                    e.right.data(), (size_t)header.taps * sizeof(float));
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

    BRIDGE_LOGI("dirs=%u taps=%u result=%d",
                header.positions, header.taps, (int)result);

    return result;
}


}
