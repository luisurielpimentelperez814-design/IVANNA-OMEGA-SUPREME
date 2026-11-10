// test_omega_pcm_format.cpp — regresión v2.5.382 (sin audio + tronidos).
//
// La v2.5.382 puso el default de creación del efecto en PCM_16_BIT (0x01).
// Pero AudioFlinger entrega el buffer del efecto en FLOAT (el mixer mezcla en
// float); el PCM16 que mide la sonda PAL es DESPUÉS del mixer (salida del
// HAL), no en el efecto. Con PCM16 por defecto, frameBytes era 4 en vez de 8
// (solo se copiaba la mitad del buffer), el deinterleave leía float como
// int16 (basura) y el write escribía int16 en un buffer float (basura) →
// SIN AUDIO + TRONIDOS al cambiar de app. La v2.5.380 (config en ceros →
// formato 0 → trato float) sonaba limpia, lo que prueba que el buffer real
// es float. Este test fija el default en PCM_FLOAT y el round-trip por
// formato, para que la regresión no vuelva.
//
// Compila en host sin NDK ni root.
#include "include/omega_pcm_format.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace ivanna;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else         { std::printf("ok:   %s\n", msg); } \
} while (0)

static bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }

int main() {
    // 1. El default DEBE ser PCM_FLOAT (0x05). Cambiarlo a PCM_16_BIT
    // reintroduce la regresión de la v2.5.382.
    CHECK(kOmegaDefaultFormat == 0x05u, "default format es PCM_FLOAT (0x05), no PCM16");
    CHECK(kOmegaDefaultSampleRate == 48000u, "default sample rate es 48000");

    // 2. bytes_per_sample por formato.
    CHECK(omega_bytes_per_sample(0x01u) == 2u, "PCM16 = 2 bytes/muestra");
    CHECK(omega_bytes_per_sample(0x05u) == 4u, "PCM_FLOAT = 4 bytes/muestra");
    CHECK(omega_bytes_per_sample(0x03u) == 4u, "PCM32 = 4 bytes/muestra");
    CHECK(omega_bytes_per_sample(0x06u) == 3u, "PCM24_PACKED = 3 bytes/muestra");
    CHECK(omega_bytes_per_sample(0x00u) == 4u, "formato 0 (calloc) = 4 bytes (trato float, como la 380)");

    // 3. frameBytes estéreo con el default: 4 bytes/muestra * 2 canales = 8.
    // Con PCM16 eran 4: solo se copiaba la mitad del buffer (la regresión).
    CHECK(omega_bytes_per_sample(kOmegaDefaultFormat) * 2u == 8u,
          "frameBytes estereo con default = 8 (no 4 como la 382)");

    // 4. Round-trip FLOAT: deinterleave + write = identidad (bit-exacto salvo
    // clamp, que no actúa en [-1, 1]).
    {
        constexpr int kFrames = 256;
        std::vector<float> in(2 * kFrames);
        for (int n = 0; n < kFrames; ++n) {
            in[2 * n] = 0.5f * std::sin(2.0f * 3.14159265f * n / 48.0f);
            in[2 * n + 1] = -0.25f * std::cos(2.0f * 3.14159265f * n / 64.0f);
        }
        std::vector<float> l(kFrames), r(kFrames);
        omega_deinterleave_chunk(in.data(), 0x05u, 0, kFrames, l.data(), r.data());
        std::vector<float> out(2 * kFrames, 0.0f);
        for (int n = 0; n < kFrames; ++n) {
            omega_write_output_frame(out.data(), 0x05u, n, l[n], r[n]);
        }
        bool identical = std::memcmp(in.data(), out.data(), sizeof(float) * 2 * kFrames) == 0;
        CHECK(identical, "round-trip FLOAT es identidad (sin silencio ni basura)");
        // Y con el default (que es float) también.
        std::vector<float> l2(kFrames), r2(kFrames);
        omega_deinterleave_chunk(in.data(), kOmegaDefaultFormat, 0, kFrames, l2.data(), r2.data());
        bool same = true;
        for (int n = 0; n < kFrames; ++n) {
            if (!near(l2[n], in[2 * n], 1e-7f) || !near(r2[n], in[2 * n + 1], 1e-7f)) same = false;
        }
        CHECK(same, "deinterleave con formato default reproduce el buffer float");
    }

    // 5. Round-trip PCM16: deinterleave + write = identidad salvo cuantización
    // de 1 LSB.
    {
        constexpr int kFrames = 128;
        std::vector<int16_t> in(2 * kFrames);
        for (int n = 0; n < kFrames; ++n) {
            in[2 * n] = static_cast<int16_t>(12000 * std::sin(n * 0.3f));
            in[2 * n + 1] = static_cast<int16_t>(-8000 * std::cos(n * 0.2f));
        }
        std::vector<float> l(kFrames), r(kFrames);
        omega_deinterleave_chunk(in.data(), 0x01u, 0, kFrames, l.data(), r.data());
        std::vector<int16_t> out(2 * kFrames, 0);
        for (int n = 0; n < kFrames; ++n) {
            omega_write_output_frame(out.data(), 0x01u, n, l[n], r[n]);
        }
        bool close = true;
        for (int n = 0; n < 2 * kFrames; ++n) {
            if (std::abs((int)out[n] - (int)in[n]) > 1) close = false;
        }
        CHECK(close, "round-trip PCM16 es identidad salvo 1 LSB");
    }

    // 6. La regresión demostrada: un buffer FLOAT interpretado como PCM16
    // (lo que hacía la 382) NO reproduce el audio — la salida difiere
    // radicalmente de la entrada. Si este test fallara (es decir, si PCM16
    // sobre float diera lo mismo), el default PCM16 sería inocuo; como
    // difiere, el default DEBE ser float.
    {
        constexpr int kFrames = 64;
        std::vector<float> in(2 * kFrames);
        for (int n = 0; n < kFrames; ++n) {
            in[2 * n] = 0.6f * std::sin(n * 0.5f);
            in[2 * n + 1] = 0.4f * std::cos(n * 0.4f);
        }
        std::vector<float> l(kFrames), r(kFrames);
        omega_deinterleave_chunk(in.data(), 0x01u, 0, kFrames, l.data(), r.data());
        float maxDiff = 0.0f;
        for (int n = 0; n < kFrames; ++n) {
            maxDiff = std::fmax(maxDiff, std::fabs(l[n] - in[2 * n]));
            maxDiff = std::fmax(maxDiff, std::fabs(r[n] - in[2 * n + 1]));
        }
        CHECK(maxDiff > 0.1f, "float leido como PCM16 difiere mucho (la 382 corrompia el audio)");
    }

    // 7. NaN/Inf en float se sanean a 0 (no se propagan como ruido).
    {
        float in[4] = {0.5f, std::nanf(""), std::numeric_limits<float>::infinity(), -0.5f};
        float l[2] = {9.0f, 9.0f}, r[2] = {9.0f, 9.0f};
        omega_deinterleave_chunk(in, 0x05u, 0, 2, l, r);
        CHECK(near(l[0], 0.5f, 1e-7f) && near(r[0], 0.0f, 1e-7f) &&
              near(l[1], 0.0f, 1e-7f) && near(r[1], -0.5f, 1e-7f),
              "NaN/Inf en float se sanean a 0");
    }

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
