// ============================================================================
// ivanna_servo.h — el CEREBRO del ASRC. v2.5.396
//
// Estima la deriva de reloj por write-timing con el estimador de Theil-Sen
// (mediana de pendientes por pares): robusto a outliers, sin supuestos de
// distribución, y barato para ventanas pequeñas.
//
// El reloj del Moto G85 es write-timing (la música es mixer regular, mmap da
// -ENOSYS), así que la única magnitud observable es: cuántos frames ha escrito
// el efecto y en qué instante monotónico. De ahí sale la tasa real:
//
//     rate = Δframes / Δt        (Hz)
//     ppm  = (rate / 48000 - 1) * 1e6
//
// Y la corrección que cancela esa deriva es:
//
//     ratio = 1 / (1 + ppm*1e-6)
//
// porque el flujo capturado a 48000*(1+d) contiene un tono de 1000 Hz que
// "mide" 1000*(1+d) en tiempo nominal; resamplear por 1/(1+d) lo devuelve a
// 1000.000 Hz. Ese es exactamente el test `drift_correction`.
// ============================================================================
#ifndef IVANNA_SERVO_H
#define IVANNA_SERVO_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ivanna {
namespace asrcd {

class TheilSenServo {
public:
    TheilSenServo(size_t window = 48, double nominal_hz = 48000.0,
                  double rail_ppm = 500.0);

    // Muestra de write-timing: instante monotónico y frames acumulados.
    void add(int64_t t_ns, int64_t frames);
    void reset();

    // Mediana de pendientes por pares -> ppm respecto al nominal.
    // false si no hay >= 2 muestras con Δt > 0.
    bool estimate(double* ppm_out) const;

    // ratio = 1/(1+ppm*1e-6), con clamp al riel.
    static double ratioFor(double ppm, double rail_ppm = 500.0);

    // Limitador de slew: evita que un salto del estimador module la ganancia
    // audiblemente (el "bombeo" del que se quejaba el usuario). Se expresa en
    // ppm de cambio máximo por actualización.
    static double slew(double current_ratio, double target_ratio,
                       double max_step_ppm = 5.0);

    size_t samples() const { return w_.size(); }
    bool ready() const;

private:
    struct Pair { int64_t t_ns; int64_t frames; };
    std::vector<Pair> w_;
    size_t cap_;
    double nominal_;
    double rail_;
};

}  // namespace asrcd
}  // namespace ivanna

#endif  // IVANNA_SERVO_H
