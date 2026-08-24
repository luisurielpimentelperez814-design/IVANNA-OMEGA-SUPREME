// test_ihr1_format.cpp — barrera de regresion del lector IHR1 unico.
//
// Cubre exactamente lo que rompio en produccion:
//   1. Layout AZ (tools/hrtf/sofa_to_ihr1.py) se lee alineado.
//   2. Layout AZEL (tools/sofa_to_ihr1.py) se lee alineado.
//   3. Un dataset denso (1250 pos, como CIPIC) NO se rechaza: el guard
//      anterior de synthetic_hrtf.hpp cortaba en 1024 y tiraba el dataset
//      real al fallback sintetico sin decir nada.
//   4. Un fichero truncado se rechaza en vez de publicar HRIR a ceros.
//
// Compilar: g++ -std=c++17 -I app/src/main/cpp tests/hrtf/test_ihr1_format.cpp
#include "spatial/ihr1_format.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ivanna::ihr1;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// Muestra deterministica: distinta por posicion y por tap, con signo
// opuesto entre L y R — cualquier desalineamiento salta a la vista.
static float sampleL(int pos, int tap) { return pos * 1000.f + tap; }
static float sampleR(int pos, int tap) { return -(pos * 1000.f + tap); }

static void writeIhr1(const std::string& path, bool azel, int numPos, int taps) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite("IHR1", 1, 4, f);
    int32_t a = numPos, b = taps, c = 48000;
    std::fwrite(&a, 4, 1, f); std::fwrite(&b, 4, 1, f); std::fwrite(&c, 4, 1, f);

    if (azel) {
        for (int i = 0; i < numPos; ++i) {
            float az = i * 0.25f, el = i * 0.5f;
            std::fwrite(&az, 4, 1, f); std::fwrite(&el, 4, 1, f);
        }
        for (int i = 0; i < numPos; ++i) {
            for (int k = 0; k < taps; ++k) { float v = sampleL(i, k); std::fwrite(&v, 4, 1, f); }
            for (int k = 0; k < taps; ++k) { float v = sampleR(i, k); std::fwrite(&v, 4, 1, f); }
        }
    } else {
        for (int i = 0; i < numPos; ++i) {
            float az = i * 0.25f;
            std::fwrite(&az, 4, 1, f);
            for (int k = 0; k < taps; ++k) { float v = sampleL(i, k); std::fwrite(&v, 4, 1, f); }
            for (int k = 0; k < taps; ++k) { float v = sampleR(i, k); std::fwrite(&v, 4, 1, f); }
        }
    }
    std::fclose(f);
}

static void checkRoundTrip(bool azel, int numPos, int taps, const char* label) {
    const std::string path = std::string("/tmp/ihr1_") + label + ".ihr1";
    writeIhr1(path, azel, numPos, taps);

    Dataset ds;
    CHECK(read(path.c_str(), ds), (std::string("lectura fallida: ") + label).c_str());
    if (!ds.valid()) return;

    CHECK(ds.layout == (azel ? Layout::AzEl : Layout::Az), "layout autodetectado incorrecto");
    CHECK(ds.numPositions() == numPos, "numPositions incorrecto");
    CHECK(ds.irLen == taps, "irLen incorrecto");
    CHECK(ds.sampleRateHz == 48000, "sampleRate incorrecto");

    // Ultima posicion: es donde se acumula todo el desalineamiento.
    const int last = numPos - 1;
    CHECK(ds.az[last] == last * 0.25f, "azimut de la ultima posicion desalineado");
    CHECK(ds.el[last] == (azel ? last * 0.5f : 0.f), "elevacion incorrecta");
    for (int k = 0; k < taps; k += (taps / 4 > 0 ? taps / 4 : 1)) {
        const size_t off = static_cast<size_t>(last) * static_cast<size_t>(taps) + k;
        CHECK(ds.L[off] == sampleL(last, k), "HRIR L desalineado");
        CHECK(ds.R[off] == sampleR(last, k), "HRIR R desalineado");
    }
    std::remove(path.c_str());
}

int main() {
    checkRoundTrip(false, 64, 32, "az");
    checkRoundTrip(true,  64, 32, "azel");

    // Densidad CIPIC real: el guard antiguo (>1024) la rechazaba.
    checkRoundTrip(true, 1250, 8, "cipic_denso");

    // Truncado: debe rechazarse, nunca publicarse a medias.
    const std::string trunc = "/tmp/ihr1_trunc.ihr1";
    writeIhr1(trunc, true, 32, 16);
    FILE* f = std::fopen(trunc.c_str(), "rb");
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fclose(f);
    f = std::fopen(trunc.c_str(), "rb");
    std::vector<char> buf(static_cast<size_t>(size) - 64);
    std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    f = std::fopen(trunc.c_str(), "wb");
    std::fwrite(buf.data(), 1, buf.size(), f);
    std::fclose(f);

    Dataset ds;
    CHECK(!read(trunc.c_str(), ds), "un IHR1 truncado se acepto como valido");
    std::remove(trunc.c_str());

    // Magic ajeno: rechazo limpio.
    const std::string bad = "/tmp/ihr1_bad.ihr1";
    f = std::fopen(bad.c_str(), "wb");
    std::fwrite("NOPE----------------", 1, 20, f);
    std::fclose(f);
    Dataset ds2;
    CHECK(!read(bad.c_str(), ds2), "un fichero sin magic IHR1 se acepto");
    std::remove(bad.c_str());

    if (failures == 0) { std::printf("test_ihr1_format: OK\n"); return 0; }
    std::printf("test_ihr1_format: %d fallo(s)\n", failures);
    return 1;
}
