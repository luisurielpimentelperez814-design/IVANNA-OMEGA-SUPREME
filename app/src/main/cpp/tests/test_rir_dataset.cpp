// tests/test_rir_dataset.cpp — valida RirDataset contra el dataset REAL
// shippeado (200 salas), no contra fixtures inventados. Si el CSV o los
// .wav cambian de forma incompatible, este test lo detecta.
#include "../spatial/RirDataset.hpp"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef IVANNA_RIR_DATASET_DIR
#error "IVANNA_RIR_DATASET_DIR no definido — ver tests/CMakeLists.txt"
#endif

static int g_failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("FALLO: %s (línea %d)\n", #cond, __LINE__); \
        ++g_failures; \
    } \
} while (0)

// RT60 por T20 (integral de Schroeder hacia atras, ajuste lineal -5..-25 dB, extrapolado a -60 dB).
// Implementacion independiente de tools/rir/measure_rt60.py: ambas deben coincidir con la columna del metadata.
static double measureT20(const std::vector<float>& x, int fs) {
    const size_t n = x.size();
    if (n < 64 || fs <= 0) return -1.0;
    std::vector<double> e(n);
    double acc = 0.0;
    for (size_t i = n; i-- > 0;) { acc += static_cast<double>(x[i]) * x[i]; e[i] = acc; }
    if (e[0] <= 0.0) return -1.0;
    size_t i5 = 0, i25 = 0;
    for (size_t i = 0; i < n; ++i) {
        const double db = 10.0 * std::log10(std::max(e[i] / e[0], 1e-14));
        if (i5 == 0 && db <= -5.0) i5 = i;
        if (db <= -25.0) { i25 = i; break; }
    }
    if (i25 <= i5 + 8) return -1.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0; const double m = static_cast<double>(i25 - i5 + 1);
    for (size_t i = i5; i <= i25; ++i) {
        const double t = static_cast<double>(i) / fs, y = 10.0 * std::log10(std::max(e[i] / e[0], 1e-14));
        sx += t; sy += y; sxx += t * t; sxy += t * y;
    }
    const double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx);
    return slope < 0.0 ? -60.0 / slope : -1.0;
}

int main() {
    Ivanna::RirDataset ds;
    bool ok = ds.load(IVANNA_RIR_DATASET_DIR);
    for (auto& w : ds.warnings()) std::printf("WARN: %s\n", w.c_str());

    CHECK(ok);
    CHECK(ds.roomCount() == 200);

    if (ds.roomCount() > 0) {
        const auto& m0 = ds.meta(0);
        CHECK(m0.filename == "rir_0000.wav");
        CHECK(std::fabs(m0.roomWidthM - 5.963877437301715f) < 0.001f);
        CHECK(std::fabs(m0.rt60S - 0.5353670983310466f) < 0.001f);

        std::vector<float> L, R;
        int sr = 0;
        bool loaded = ds.loadImpulseResponse(0, L, R, sr);
        CHECK(loaded);
        CHECK(sr == 16000);
        CHECK(L.size() == 12848);
        CHECK(L.size() == R.size());
        for (float v : L) CHECK(v >= -1.0f && v <= 1.0f);

        // Todas las 200 salas deben cargar sin excepción/crash — no solo la 0.
        size_t loadFailures = 0;
        for (size_t i = 0; i < ds.roomCount(); ++i) {
            std::vector<float> l2, r2;
            int sr2 = 0;
            if (!ds.loadImpulseResponse(i, l2, r2, sr2) || l2.empty()) ++loadFailures;
        }
        CHECK(loadFailures == 0);
        if (loadFailures > 0) {
            std::printf("  (%zu/%zu salas fallaron al cargar)\n", loadFailures, ds.roomCount());
        }

        size_t nearest = ds.findNearestByRT60(m0.rt60S);
        CHECK(nearest == 0);

        // Constante de default calibrado — debe resolver a un índice válido
        // sobre el dataset real (mediana real de las 200 salas, no inventada).
        size_t defaultRoom = ds.findNearestByRT60(Ivanna::RirDataset::kDefaultTargetRt60S);
        CHECK(defaultRoom < ds.roomCount());
        std::printf("Sala default (RT60 mediana %.3fs) -> idx=%zu, sala real rt60=%.3fs\n",
                    Ivanna::RirDataset::kDefaultTargetRt60S, defaultRoom,
                    ds.meta(defaultRoom).rt60S);


        // ── RT60 medido: la columna 13 debe coincidir con una re-medicion nativa del WAV (tol. 3 %) ──
        // y el RT60 nominal NO es el tiempo de caida real (factor medido 2.28 +/- 0.1 en las 200 salas).
        size_t mismatchMeasured = 0, nominalWouldPass = 0; double rsum = 0.0, rmin = 1e9, rmax = 0.0;
        for (size_t i = 0; i < ds.roomCount(); ++i) {
            std::vector<float> l3, r3; int sr3 = 0;
            if (!ds.loadImpulseResponse(i, l3, r3, sr3)) continue;
            const double t20 = measureT20(l3, sr3);
            const auto& mi = ds.meta(i);
            CHECK(mi.rt60MeasuredS > 0.f);
            if (t20 <= 0.0 || std::fabs(mi.rt60MeasuredS - t20) > 0.03 * t20) ++mismatchMeasured;
            // Caso negativo: el detector debe RECHAZAR el RT60 nominal como medicion del WAV.
            if (t20 > 0.0 && std::fabs(mi.rt60S - t20) <= 0.03 * t20) ++nominalWouldPass;
            const double ratio = mi.rt60MeasuredS / std::max(mi.rt60S, 1e-6f);
            rsum += ratio; rmin = std::min(rmin, ratio); rmax = std::max(rmax, ratio);
        }
        CHECK(mismatchMeasured == 0);
        CHECK(nominalWouldPass == 0);
        const double rmean = rsum / static_cast<double>(ds.roomCount());
        CHECK(rmean > 2.2 && rmean < 2.4);
        CHECK(rmin > 2.0 && rmax < 2.6);
        std::printf("RT60 medido/nominal: media %.3f min %.3f max %.3f (n=%zu); desajustes medido %zu\n",
                    rmean, rmin, rmax, ds.roomCount(), mismatchMeasured);

        // Índice inválido debe fallar limpio, nunca crashear.
        std::vector<float> badL, badR;
        int badSr = 0;
        CHECK(!ds.loadImpulseResponse(99999, badL, badR, badSr));
        CHECK(badL.empty());
    }

    if (g_failures == 0) {
        std::printf("TODOS LOS TESTS PASARON (RirDataset, 200 salas reales).\n");
        return 0;
    }
    std::printf("%d TESTS FALLARON.\n", g_failures);
    return 1;
}
