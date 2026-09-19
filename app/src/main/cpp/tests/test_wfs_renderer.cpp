// test_wfs_renderer.cpp — verificación de WfsRenderer (Wave Field Synthesis)
// Standalone sin gtest (mismo criterio que test_audio_bus.cpp):
//   g++ -std=c++17 -Wall -Wextra -I../spatial test_wfs_renderer.cpp
//       ../spatial/WfsRenderer.cpp -o test_wfs_renderer && ./test_wfs_renderer
#include "../spatial/WfsRenderer.hpp"
#include <cmath>
#include <cstdio>
#include <vector>

using ivanna::spatial::WfsRenderer;

static int g_failures = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { std::printf("  [FAIL] %s\n", msg); ++g_failures; } \
    else         { std::printf("  [ok]   %s\n", msg); } } while (0)

static double rmsOf(const std::vector<float>& v) {
    double acc = 0.0;
    for (float x : v) acc += static_cast<double>(x) * x;
    return v.empty() ? 0.0 : std::sqrt(acc / v.size());
}
static bool allFinite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return false;
    return true;
}
// Retardo del pico de energía (muestras) por cruce de envolvente simple.
static int firstStrongSample(const std::vector<float>& v, float thresh) {
    for (size_t i = 0; i < v.size(); ++i)
        if (std::fabs(v[i]) > thresh) return static_cast<int>(i);
    return -1;
}

int main() {
    std::printf("WfsRenderer — verificación de síntesis de campo de ondas\n");
    constexpr int kFrames = 384;
    constexpr float kSr = 48000.f;

    WfsRenderer w;
    EXPECT(w.init(kSr, kFrames, 16), "init() con 16 altavoces virtuales");
    EXPECT(w.numSpeakers() == 16, "array queda con 16 altavoces");

    // Impulso unitario como fuente de prueba. OJO FÍSICA: el array virtual
    // tiene 1.5 m de radio y las fuentes hasta 8 m → el delay de propagación
    // llega a ~33 ms ≈ 4 bloques de 384. Los tests con impulso procesan
    // 4 bloques consecutivos y examinan la ventana completa.
    std::vector<float> impulse(kFrames, 0.f);
    impulse[0] = 1.0f;
    std::vector<float> silence(kFrames, 0.f);
    const float* inPtr[1]  = { impulse.data() };
    const float* silPtr[1] = { silence.data() };

    // ── Caso 1: objeto centrado al frente → L y R simétricos ──
    {
        WfsRenderer wfs; wfs.init(kSr, kFrames, 16);
        wfs.setObject(0, 0.0f, 2.0f, 1.0f);   // 2 m al frente
        std::vector<float> L(kFrames * 4, 0.f), R(kFrames * 4, 0.f);
        wfs.process(inPtr, 1, L.data(), R.data(), kFrames);
        for (int b = 1; b < 4; ++b)
            wfs.process(silPtr, 1, L.data() + b * kFrames, R.data() + b * kFrames, kFrames);
        EXPECT(allFinite(L) && allFinite(R), "frente: sin NaN/Inf");
        EXPECT(rmsOf(L) > 1e-6 && rmsOf(R) > 1e-6, "frente: hay energía en ambos oídos");
        const double sym = std::fabs(rmsOf(L) - rmsOf(R)) / (rmsOf(L) + rmsOf(R));
        EXPECT(sym < 0.15, "frente: energía L≈R (simetría de campo centrado)");
        const int aL = firstStrongSample(L, 0.01f), aR = firstStrongSample(R, 0.01f);
        EXPECT(aL >= 0 && aR >= 0 && std::abs(aL - aR) <= 2,
               "frente: llegada simultánea a ambos oídos (ITD ≈ 0, ±2 muestras)");
    }

    // ── Caso 2: objeto a la derecha → R más fuerte y antes ──
    {
        WfsRenderer wfs; wfs.init(kSr, kFrames, 16);
        wfs.setObject(0, 2.0f, 0.0f, 1.0f);   // 2 m a la derecha
        std::vector<float> L(kFrames * 4, 0.f), R(kFrames * 4, 0.f);
        wfs.process(inPtr, 1, L.data(), R.data(), kFrames);
        for (int b = 1; b < 4; ++b)
            wfs.process(silPtr, 1, L.data() + b * kFrames, R.data() + b * kFrames, kFrames);
        EXPECT(rmsOf(R) > rmsOf(L) * 1.2, "derecha: ILD correcto (R > L)");
        const int aL = firstStrongSample(L, 0.005f), aR = firstStrongSample(R, 0.005f);
        EXPECT(aR >= 0 && (aL < 0 || aR <= aL), "derecha: llega antes (o igual) al oído derecho");
    }

    // ── Caso 3: atenuación física 1/sqrt(d) — lejos suena más bajo ──
    {
        WfsRenderer nearW; nearW.init(kSr, kFrames, 16);
        nearW.setObject(0, 0.0f, 1.0f, 1.0f);
        std::vector<float> Ln(kFrames * 4, 0.f), Rn(kFrames * 4, 0.f);
        nearW.process(inPtr, 1, Ln.data(), Rn.data(), kFrames);
        for (int b = 1; b < 4; ++b)
            nearW.process(silPtr, 1, Ln.data() + b * kFrames, Rn.data() + b * kFrames, kFrames);

        WfsRenderer farW; farW.init(kSr, kFrames, 16);
        farW.setObject(0, 0.0f, 6.0f, 1.0f);
        std::vector<float> Lf(kFrames * 4, 0.f), Rf(kFrames * 4, 0.f);
        farW.process(inPtr, 1, Lf.data(), Rf.data(), kFrames);
        for (int b = 1; b < 4; ++b)
            farW.process(silPtr, 1, Lf.data() + b * kFrames, Rf.data() + b * kFrames, kFrames);

        EXPECT(rmsOf(Ln) > rmsOf(Lf) * 1.3, "distancia: fuente cercana claramente más intensa (1/sqrt d)");
    }

    // ── Caso 4: varios objetos y estabilidad de larga duración ──
    {
        WfsRenderer wfs; wfs.init(kSr, kFrames, 16);
        wfs.setObject(0, 0.0f, 2.0f, 1.0f);
        wfs.setObject(1, -1.5f, 1.0f, 0.8f);
        wfs.setObject(2, 1.5f, 1.0f, 0.8f);
        std::vector<float> sig(kFrames);
        for (int i = 0; i < kFrames; ++i)
            sig[i] = std::sin(2.f * 3.14159265f * 440.f * static_cast<float>(i) / kSr) * 0.5f;
        const float* ins[3] = { sig.data(), sig.data(), sig.data() };
        std::vector<float> L(kFrames, 0.f), R(kFrames, 0.f);
        for (int b = 0; b < 64; ++b) {
            // process() ACUMULA (+=) sobre la salida — los buffers se
            // limpian por bloque, como haría la cadena real.
            std::fill(L.begin(), L.end(), 0.f);
            std::fill(R.begin(), R.end(), 0.f);
            wfs.process(ins, 3, L.data(), R.data(), kFrames);
            EXPECT(allFinite(L) && allFinite(R), "multi-objeto: sin NaN/Inf (estable)");
            EXPECT(rmsOf(L) < 4.0 && rmsOf(R) < 4.0, "multi-objeto: sin blow-up de ganancia");
        }
    }

    // ── Caso 5: SIN MICRO-CORTES al mover una fuente en caliente ──
    // Regresión del bug rebuildDelays(): antes, cada setObject() hacía
    // assign() de TODAS las líneas de delay → historial de audio borrado
    // → discontinuidad instantánea = click/pop audible. Ahora el movimiento
    // suaviza delay/ganancia por muestra sin tocar el historial: el delta
    // muestra-a-muestra debe quedar acotado por el slew natural de la señal.
    {
        WfsRenderer wfs; wfs.init(kSr, kFrames, 16);
        wfs.setObject(0, 0.0f, 2.0f, 1.0f);
        std::vector<float> sig(kFrames);
        for (int i = 0; i < kFrames; ++i)
            sig[i] = std::sin(2.f * 3.14159265f * 440.f * static_cast<float>(i) / kSr) * 0.5f;
        const float* ins[1] = { sig.data() };
        std::vector<float> L(kFrames, 0.f), R(kFrames, 0.f);
        // 8 bloques de asentamiento (delay físico ~2 m ≈ 280 muestras)
        for (int b = 0; b < 8; ++b) {
            std::fill(L.begin(), L.end(), 0.f);
            std::fill(R.begin(), R.end(), 0.f);
            wfs.process(ins, 1, L.data(), R.data(), kFrames);
        }
        float maxDeltaBefore = 0.f;
        for (int i = 1; i < kFrames; ++i) {
            maxDeltaBefore = std::fmax(maxDeltaBefore, std::fabs(L[i] - L[i-1]));
            maxDeltaBefore = std::fmax(maxDeltaBefore, std::fabs(R[i] - R[i-1]));
        }
        // Movimiento BRUSCO de la fuente en mitad del stream:
        wfs.setObject(0, 3.0f, 0.5f, 1.0f);
        float maxDeltaAfter = 0.f, maxAbsAfter = 0.f;
        for (int b = 0; b < 8; ++b) {
            std::fill(L.begin(), L.end(), 0.f);
            std::fill(R.begin(), R.end(), 0.f);
            wfs.process(ins, 1, L.data(), R.data(), kFrames);
            for (int i = 1; i < kFrames; ++i) {
                maxDeltaAfter = std::fmax(maxDeltaAfter, std::fabs(L[i] - L[i-1]));
                maxDeltaAfter = std::fmax(maxDeltaAfter, std::fabs(R[i] - R[i-1]));
                maxAbsAfter = std::fmax(maxAbsAfter, std::fabs(L[i]));
                maxAbsAfter = std::fmax(maxAbsAfter, std::fabs(R[i]));
            }
        }
        EXPECT(allFinite(L) && allFinite(R), "movimiento: sin NaN/Inf");
        // El slew tras mover la fuente debe ser comparable al slew natural de
        // la señal (mismo orden de magnitud). Un micro-corte del bug viejo
        // (señal → silencio → señal en 1 muestra) daría deltas 10-100× mayores.
        EXPECT(maxDeltaAfter < maxDeltaBefore * 4.0f + 0.02f,
               "movimiento: sin micro-cortes (delta acotado por el slew natural)");
        EXPECT(maxAbsAfter < 2.0f, "movimiento: sin picos de glitch en amplitud");
    }

    // ── Caso 6: eliminar una fuente sonando NO produce click (fade-out) ──
    {
        WfsRenderer wfs; wfs.init(kSr, kFrames, 16);
        wfs.setObject(0, 0.0f, 2.0f, 1.0f);
        std::vector<float> sig(kFrames);
        for (int i = 0; i < kFrames; ++i)
            sig[i] = std::sin(2.f * 3.14159265f * 440.f * static_cast<float>(i) / kSr) * 0.5f;
        const float* ins[1] = { sig.data() };
        std::vector<float> L(kFrames, 0.f), R(kFrames, 0.f);
        for (int b = 0; b < 8; ++b) {
            std::fill(L.begin(), L.end(), 0.f);
            std::fill(R.begin(), R.end(), 0.f);
            wfs.process(ins, 1, L.data(), R.data(), kFrames);
        }
        wfs.removeObject(0);   // corte solicitado a mitad de stream
        float maxDelta = 0.f;
        for (int b = 0; b < 4; ++b) {
            std::fill(L.begin(), L.end(), 0.f);
            std::fill(R.begin(), R.end(), 0.f);
            wfs.process(ins, 1, L.data(), R.data(), kFrames);  // sin entrada viva
            for (int i = 1; i < kFrames; ++i) {
                maxDelta = std::fmax(maxDelta, std::fabs(L[i] - L[i-1]));
                maxDelta = std::fmax(maxDelta, std::fabs(R[i] - R[i-1]));
            }
        }
        EXPECT(allFinite(L) && allFinite(R), "removeObject: sin NaN/Inf");
        EXPECT(maxDelta < 0.10f, "removeObject: fade-out sin click (delta acotado)");
        EXPECT(wfs.numObjects() == 0, "removeObject: la ranura se libera tras el fade");
    }

    std::printf("\n====================================================\n");
    if (g_failures == 0) { std::printf("TODOS LOS TESTS PASARON.\n"); return 0; }
    std::printf("%d TEST(S) FALLARON.\n", g_failures);
    return 1;

    // ── Caso NUEVO (2026-09-18): retiro de fuente sin tronidos ──
    {
        WfsRenderer wfs; wfs.init(kSr, kFrames, 16);
        wfs.setObject(0, 0.0f, 2.0f, 1.0f);
        std::vector<float> tone(kFrames);
        for (int i = 0; i < kFrames; ++i) tone[i] = 0.5f * std::sin(2.f * 3.14159265f * 440.f * i / kSr);
        const float* tonePtr[1] = { tone.data() };
        std::vector<float> L(kFrames, 0.f), R(kFrames, 0.f);
        wfs.process(tonePtr, 1, L.data(), R.data(), kFrames);
        wfs.removeObject(0);
        std::vector<float> L2(kFrames, 0.f), R2(kFrames, 0.f);
        wfs.process(silPtr, 1, L2.data(), R2.data(), kFrames);
        float maxJump = 0.f;
        for (int i = 1; i < kFrames; ++i) {
            maxJump = std::max(maxJump, std::fabs(L2[i] - L2[i-1]));
            maxJump = std::max(maxJump, std::fabs(R2[i] - R2[i-1]));
        }
        EXPECT(allFinite(L2) && allFinite(R2), "retiro: sin NaN/Inf durante el fade");
        EXPECT(maxJump < 0.2f, "retiro: SIN tronido (salto muestra-a-muestra acotado)");
    }

    // ── Caso NUEVO (2026-09-18): suma coherente sin clip (anti-tronidos) ──
    {
        WfsRenderer wfs; wfs.init(kSr, kFrames, 16);
        std::vector<float> hot(kFrames, 0.9f);
        const float* hotPtr[4] = { hot.data(), hot.data(), hot.data(), hot.data() };
        for (int o = 0; o < 4; ++o) wfs.setObject(o, 0.0f, 1.5f, 2.0f);
        std::vector<float> L(kFrames, 0.f), R(kFrames, 0.f);
        for (int b = 0; b < 4; ++b)
            wfs.process(hotPtr, 4, L.data(), R.data(), kFrames);
        EXPECT(allFinite(L) && allFinite(R), "coherente: sin NaN/Inf con 4 fuentes a gain 2");
        float peak = 0.f;
        for (float v : L) peak = std::max(peak, std::fabs(v));
        for (float v : R) peak = std::max(peak, std::fabs(v));
        EXPECT(peak < 2.0f, "coherente: techo del soft-limit (sin clip duro => sin tronido)");
        EXPECT(peak > 0.1f, "coherente: hay senal (el soft-limit no anula el campo)");
    }

}
