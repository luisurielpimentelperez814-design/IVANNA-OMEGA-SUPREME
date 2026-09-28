#pragma once
#include <cmath>
#include "dsp_types.h"

namespace ivanna {
class ParametricEQ {
public:
    ParametricEQ() noexcept;
    void reset() noexcept;
    void setSampleRate(float sr) noexcept;
    void setBand(int band, float freq, float q, float gainDb) noexcept;
    void setParams(const DSPParams& p) noexcept;
    void process(float* left, float* right, int frames) noexcept;
    // Compensación de ganancia de salida calculada en setParams() cuando las
    // bandas apiladas superan 6 dB — el JNI la lee y la resta de g_params.master
    // antes de pasárselo a g_gain, manteniendo el headroom del SafetyLimiter.
    float getOutputCompensationDb() const noexcept { return eqOutputCompensationDb_; }
private:
    struct Biquad {
        float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
        inline float processSample(float x) noexcept {
            // NaN guard de entrada: si llega NaN/Inf (estado previo roto o
            // señal saturada) limpia el estado y pasa silencio — evita que
            // el NaN se propague por todos los biquads en cascada y llegue
            // al SafetyLimiter que lo clampea a 0 con un tronido audible.
            if (__builtin_expect(!std::isfinite(x), 0)) { reset(); return 0.f; }
            float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            // NaN guard de salida: coeficientes inestables con señal fuera de
            // rango producen y=Inf/NaN desde la primera iteración. Al detectarlo
            // se limpia el estado para evitar la propagación al bloque siguiente.
            if (__builtin_expect(!std::isfinite(y), 0)) { reset(); return 0.f; }
            x2=x1; x1=x; y2=y1; y1=y; return y;
        }
        void reset() noexcept { x1=x2=y1=y2=0; }
    };
    static constexpr int NUM_BANDS = 8;
    Biquad bandsL[NUM_BANDS];
    Biquad bandsR[NUM_BANDS];
    // Transparencia absoluta: una banda a 0 dB es matematicamente la
    // identidad, pero procesarla igual acumula ruido de cuantizacion float
    // (8 biquads en cascada => ~8x el ruido de redondeo) y gasta CPU.
    // active_[b] = false => la banda se salta bit-exacto.
    bool active_[NUM_BANDS] = {false,false,false,false,false,false,false,false};
    float sampleRate_ = 96000.0f;
    // Compensación de headroom calculada en setParams() — ver getter.
    float eqOutputCompensationDb_ = 0.f;

    // ── Anti-zipper (crossfade de coeficientes, ~15 ms) ────────────────────
    // setBand() recalcula los 5 coeficientes del biquad en un instante; con
    // la misma estructura direct-form-II y estado heredado, el salto de
    // b0/b1/b2/a1/a2 produce un clic audible al arrastrar cualquier fader de
    // EQ. Interpolar coeficientes por muestra atraviesa filtros intermedios
    // potencialmente inestables con Q alto; la solucion segura y estandar es
    // crossfade temporal: durante ~15 ms se procesa la banda vieja (estado
    // intacto) y la nueva en paralelo, mezclando linealmente old->new.
    Biquad prevL[NUM_BANDS];
    Biquad prevR[NUM_BANDS];
    int fade_[NUM_BANDS] = {};
    int fadeLen_[NUM_BANDS] = {};
};
}
