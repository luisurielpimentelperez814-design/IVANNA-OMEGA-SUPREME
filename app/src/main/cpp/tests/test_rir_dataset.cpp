// tests/test_rir_dataset.cpp — valida RirDataset contra el dataset REAL
// shippeado (200 salas), no contra fixtures inventados. Si el CSV o los
// .wav cambian de forma incompatible, este test lo detecta.
#include "../spatial/RirDataset.hpp"
#include <cstdio>
#include <cmath>

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
