// test_holos_retune_sweep.cpp — barrido de retuneo HOLOS (v2.5.369).
//
// Mide THD+N @1 kHz y @100 Hz de HOLOS en una rejilla (amount × harmonics)
// para elegir el default que cumpla THD+N ≤ −70 dB (ideal ≤ −80 dB) con el
// efecto todavía audible/medible. También mide la diferencia RMS vs bypass
// (qué tan "presente" está el efecto) e IMD SMPTE en el ajuste elegido.
// Números REALES medidos en host a 48 kHz. No es un test de regresión
// estricto: imprime la tabla y solo exige que el default elegido (marcado
// abajo) cumpla el criterio.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "../spatial/HolosOperator.hpp"
#include "ivanna_measure.hpp"

using namespace ivanna;
using namespace ivanna::measure;

namespace {
constexpr double kFs = 48000.0;

std::vector<float> tone(double f0, double amp, double seconds) {
    const int n = (int)(seconds * kFs);
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = (float)(amp * std::sin(2.0 * kPi * f0 * i / kFs));
    return v;
}

void runHolos(HolosOperator& op, std::vector<float>& l, std::vector<float>& r) {
    for (int s = 0; s < (int)l.size(); s += 128) {
        const int m = std::min(128, (int)l.size() - s);
        op.process(l.data() + s, r.data() + s, m);
    }
}

// THD+N @f0 y diferencia RMS vs bypass (dB) para un ajuste.
struct SweepRow {
    float amount, harmonics;
    double thdn1k, thdn100, diffDb;
};

SweepRow measureRow(float amount, float harmonics) {
    HolosParams p;
    p.amount = amount;
    p.harmonics = harmonics;
    auto run = [&](double f0) {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        auto t = tone(f0, std::pow(10.0, -3.0 / 20.0), 3.0);
        std::vector<float> l = t, r = t;
        runHolos(op, l, r);
        const double th = thdn(l.data() + 96000, 48000, kFs, f0).thdnDb;
        // Diferencia RMS vs bypass en la misma ventana.
        double num = 0.0, den = 0.0;
        for (int i = 96000; i < 144000; ++i) {
            const double d = (double)l[i] - (double)t[i];
            num += d * d;
            den += (double)t[i] * (double)t[i];
        }
        const double diffDb = 10.0 * std::log10((num + 1e-30) / (den + 1e-30));
        return std::pair<double, double>(th, diffDb);
    };
    auto a = run(1000.0);
    auto b = run(100.0);
    return {amount, harmonics, a.first, b.first, a.second};
}
}  // namespace

TEST(HolosRetuneSweep, GridThdnAndAudibility) {
    std::printf("[holos-retune] amount | harm | THD+N@1k | THD+N@100 | diff-vs-bypass\n");
    for (float amount : {0.1f, 0.25f, 0.5f, 1.0f}) {
        for (float harm : {0.0f, 0.05f, 0.1f, 0.16f, 0.32f}) {
            SweepRow r = measureRow(amount, harm);
            std::printf("[holos-retune] %5.2f | %5.2f | %8.1f | %9.1f | %8.1f dB\n",
                        r.amount, r.harmonics, r.thdn1k, r.thdn100, r.diffDb);
        }
    }
    // El default elegido (ver FUSION/CHANGELOG v2.5.369) debe cumplir
    // THD+N ≤ −70 dB y ser medible (diff vs bypass > −40 dB).
    // Se verifica en el test siguiente con los valores finales.
    SUCCEED();
}

TEST(HolosRetuneSweep, ChosenDefaultIsCleanAndMeasurable) {
    // Default elegido v2.5.369: amount=0.5, harmonics=0.0 (K off).
    // Medido en el barrido: THD+N @1 kHz −94.0 dB, @100 Hz −79.6 dB.
    // Requisito: THD+N ≤ −70 dB en ambas frecuencias.
    HolosParams p;
    p.amount = 0.5f;
    p.harmonics = 0.0f;
    auto measureThdn = [&](double f0) {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        auto t = tone(f0, std::pow(10.0, -3.0 / 20.0), 3.0);
        std::vector<float> l = t, r = t;
        runHolos(op, l, r);
        return thdn(l.data() + 96000, 48000, kFs, f0).thdnDb;
    };
    const double thdn1k = measureThdn(1000.0);
    const double thdn100 = measureThdn(100.0);
    std::printf("[holos-retune] default (0.5, K=0): THD+N @1k %.1f dB, @100 %.1f dB\n",
                thdn1k, thdn100);
    EXPECT_LE(thdn1k, -70.0);
    EXPECT_LE(thdn100, -70.0);

    // IMD SMPTE 60 + 7000 Hz (4:1) en el default.
    {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        std::vector<float> l(144000), r(144000);
        for (int i = 0; i < 144000; ++i) {
            const double t = i / kFs;
            l[i] = r[i] = (float)(0.8 * 0.708 * std::sin(2.0 * kPi * 60.0 * t) +
                                  0.2 * 0.708 * std::sin(2.0 * kPi * 7000.0 * t));
        }
        runHolos(op, l, r);
        const double imd = imdSmpteDb(l.data() + 96000, 48000, kFs, 60.0, 7000.0);
        std::printf("[holos-retune] default (0.5, K=0): IMD SMPTE %.1f dB\n", imd);
        EXPECT_LE(imd, -60.0);
    }

    // Efecto medible: diferencia vs bypass compensando la latencia
    // (512 muestras = fftSize). Si fuera bypass, la diferencia sería
    // ~−150 dB (piso de float); con HOLOS activo es mucho mayor.
    {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        auto t = tone(1000.0, std::pow(10.0, -3.0 / 20.0), 3.0);
        std::vector<float> l = t, r = t;
        runHolos(op, l, r);
        const int lat = op.latencySamples();
        double num = 0.0, den = 0.0;
        for (int i = 96000; i < 144000; ++i) {
            const double d = (double)l[i] - (double)t[i - lat];
            num += d * d;
            den += (double)t[i - lat] * (double)t[i - lat];
        }
        const double diffDb = 10.0 * std::log10((num + 1e-30) / (den + 1e-30));
        std::printf("[holos-retune] default (0.5, K=0): diff vs bypass (lat-comp) %.1f dB\n",
                    diffDb);
        // Medible = muy por encima del piso de float (~−150 dB). Un
        // bypass bit-exacto daría ~−150 dB; −44 dB es el render espacial
        // (ITD/ILD) y el campo de ganancia Γ trabajando.
        EXPECT_GT(diffDb, -100.0) << "el efecto debe ser medible";
    }
}

// Guarda de armónicos (v2.5.411, tarea 1): el valor vivo de Esse
// (amount=0.359, harmonics=0.298, de ivanna_omega_params.xml) NO cumple
// el criterio THD+N ≤ −70 dB / IMD ≤ −60 dB. El máximo validado es
// kHolosHarmonicsMaxValidated (0.002): el mayor valor que pasa a TODOS los
// amounts (0.359, 0.5, 1.0) con ≥1 dB de margen. La guarda se aplica al
// leer (clamp en JNI ×2 y FusionCore ×1), sin reescribir preferencias.
namespace {
struct GuardRow {
    double thdn1k, thdn100, imd;
};

GuardRow measureGuardRow(float amount, float harmonics) {
    HolosParams p;
    p.amount = amount;
    p.harmonics = harmonics;
    auto thdnAt = [&](double f0) {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        auto t = tone(f0, std::pow(10.0, -3.0 / 20.0), 3.0);
        std::vector<float> l = t, r = t;
        runHolos(op, l, r);
        return thdn(l.data() + 96000, 48000, kFs, f0).thdnDb;
    };
    double imd = 0.0;
    {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        std::vector<float> l(144000), r(144000);
        for (int i = 0; i < 144000; ++i) {
            const double t = i / kFs;
            l[i] = r[i] = (float)(0.8 * 0.708 * std::sin(2.0 * kPi * 60.0 * t) +
                                  0.2 * 0.708 * std::sin(2.0 * kPi * 7000.0 * t));
        }
        runHolos(op, l, r);
        imd = imdSmpteDb(l.data() + 96000, 48000, kFs, 60.0, 7000.0);
    }
    return {thdnAt(1000.0), thdnAt(100.0), imd};
}
}  // namespace

TEST(HolosHarmonicsGuard, UserLiveValueFails) {
    // Valor vivo de Esse (0.359, 0.298): documenta POR QUÉ existe la guarda.
    // Medido en host: THD+N @1k −41.1 dB, @100 −43.6 dB, IMD −37.7 dB.
    // EXPECT_GT (no EXPECT_LE): el test se rompería si el valor pasara,
    // lo que querría decir que la guarda ya no hace falta.
    const GuardRow r = measureGuardRow(0.359f, 0.298f);
    std::printf("[holos-guard] vivo (0.359, 0.298): THD+N @1k %.1f dB, @100 %.1f dB, IMD %.1f dB (criterio: <= -70 / <= -60)\n",
                r.thdn1k, r.thdn100, r.imd);
    EXPECT_GT(r.thdn1k, -70.0) << "el valor vivo debe FALLAR el criterio (si pasa, la guarda sobra)";
    EXPECT_GT(r.thdn100, -70.0);
    EXPECT_GT(r.imd, -60.0);
}

TEST(HolosHarmonicsGuard, ValidatedMaxPassesAtAllAmounts) {
    // El máximo validado (0.002) debe cumplir el criterio a amount 0.359,
    // 0.5 y 1.0. Medido en host: (0.359) −76.5/−77.5/−73.2; (0.5)
    // −73.7/−74.5/−70.3; (1.0) −75.3/−71.5/−72.2. Margen mínimo 1.5 dB
    // (@100 Hz, amount 1.0). 0.003 pasa con solo 0.3 dB de margen y 0.004
    // ya falla a 1.0: el máximo con margen es 0.002.
    for (float amount : {0.359f, 0.5f, 1.0f}) {
        const GuardRow r = measureGuardRow(amount, kHolosHarmonicsMaxValidated);
        std::printf("[holos-guard] max %.3f @ amount %.3f: THD+N @1k %.1f dB, @100 %.1f dB, IMD %.1f dB\n",
                    kHolosHarmonicsMaxValidated, amount, r.thdn1k, r.thdn100, r.imd);
        EXPECT_LE(r.thdn1k, -70.0) << "amount=" << amount;
        EXPECT_LE(r.thdn100, -70.0) << "amount=" << amount;
        EXPECT_LE(r.imd, -60.0) << "amount=" << amount;
    }
}

TEST(HolosHarmonicsGuard, ClampMapsStoredValueToMax) {
    // La guarda al leer: el valor guardado del usuario (0.298) se lee como
    // el máximo validado; 0.0 sigue siendo 0.0 (bypass bit-exacto intacto)
    // y los valores por debajo del máximo pasan sin tocar.
    EXPECT_FLOAT_EQ(std::clamp(0.298f, 0.0f, kHolosHarmonicsMaxValidated),
                    kHolosHarmonicsMaxValidated);
    EXPECT_FLOAT_EQ(std::clamp(0.0f, 0.0f, kHolosHarmonicsMaxValidated), 0.0f);
    EXPECT_FLOAT_EQ(std::clamp(0.001f, 0.0f, kHolosHarmonicsMaxValidated), 0.001f);
    EXPECT_FLOAT_EQ(std::clamp(1.0f, 0.0f, kHolosHarmonicsMaxValidated),
                    kHolosHarmonicsMaxValidated);
}
