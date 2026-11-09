// IVANNA OMEGA SUPREME — Módulo de integridad anti-piratería (nativo).
// CONFIDENCIAL (interno). Ver ivanna_integrity.h y docs/SECURITY_INTERNAL.md.
#include "ivanna_integrity.h"

// Ofuscación de strings (fase 2): los literales sensibles (rutas legítimas,
// tag de log) van cifrados y solo se descifran en pila al usarlos.
#include "ivanna_obfuscate.h"

// Pin generado en build-time (tools/security/generate_build_pins.py).
// Si el header no existe (build sin generateSecurityPins), el pin es vacío.
#if defined(__has_include)
#if __has_include("ivanna_build_pins.h")
#include "ivanna_build_pins.h"
#endif
#endif

#ifndef IVANNA_PIN_CERT_SHA256
#define IVANNA_PIN_CERT_SHA256 ""
#endif

#include <atomic>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#include <dlfcn.h>
// El tag de log va cifrado (no aparece en claro en .rodata). Se descifra
// en pila en cada uso y se borra tras loguear. IVANNA_XSTR devuelve una
// referencia a un `static constexpr` (cifrado en compilación).
#define IVANNA_SEC_LOG(...)                                                    \
    do {                                                                       \
        char _ivanna_tag[32];                                                  \
        IVANNA_XSTR("IVANNA_SECURITY").decrypt(_ivanna_tag, sizeof(_ivanna_tag)); \
        __android_log_print(ANDROID_LOG_WARN, _ivanna_tag, __VA_ARGS__);       \
        ivanna::security::obf::wipe(_ivanna_tag, sizeof(_ivanna_tag));         \
    } while (0)
#else
#include <cstdio>
// Host (tests): mismo tag cifrado que en Android (IVANNA_XSTR); se descifra
// en pila y se borra. El literal no queda en claro ni en el binario host.
#define IVANNA_SEC_LOG(...)                                                    \
    do {                                                                       \
        char _ivanna_tag[32];                                                  \
        IVANNA_XSTR("IVANNA_SECURITY").decrypt(_ivanna_tag, sizeof(_ivanna_tag)); \
        fprintf(stderr, "[%s] ", _ivanna_tag);                                 \
        fprintf(stderr, __VA_ARGS__);                                          \
        ivanna::security::obf::wipe(_ivanna_tag, sizeof(_ivanna_tag));         \
    } while (0)
#endif

namespace ivanna::security {
namespace {

// ── SHA-256 (FIPS 180-4, implementación autocontenida) ─────────────────────
inline uint32_t rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

const uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

void sha256_block(const uint8_t* p, uint32_t h[8]) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(p[i * 4]) << 24) |
               (static_cast<uint32_t>(p[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(p[i * 4 + 2]) << 8) |
               static_cast<uint32_t>(p[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = hh + s1 + ch + kSha256K[i] + w[i];
        const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// Comparación en tiempo constante (no early-exit por timing).
bool ct_equal(const char* a, const char* b, size_t n) {
    if (!a || !b) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; ++i) diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

#if defined(__ANDROID__)
bool starts_with(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    const size_t n = std::strlen(prefix);
    return std::strncmp(s, prefix, n) == 0;
}
#endif

std::atomic<bool> g_gate{false};
// Cache del self-check: -1 sin calcular, 0 sospechoso, 1 legítimo.
std::atomic<int> g_self_check_cached{-1};

}  // namespace

const char* integrity_pinned_cert_sha256() {
    return IVANNA_PIN_CERT_SHA256;
}

CertCheck integrity_verify_cert_sha256(const char* hex) {
    const char* pin = IVANNA_PIN_CERT_SHA256;
    if (!pin || pin[0] == '\0') return CertCheck::kNoPin;
    if (!hex) return CertCheck::kMismatch;
    // El pin es SHA-256 hex: exactamente 64 caracteres minúsculos.
    if (std::strlen(hex) != 64 || std::strlen(pin) != 64) return CertCheck::kMismatch;
    return ct_equal(hex, pin, 64) ? CertCheck::kOk : CertCheck::kMismatch;
}

bool integrity_sha256(const uint8_t* in, size_t len, uint8_t out[32]) {
    if (!in || !out) return false;
    uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                     0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint64_t bitlen = static_cast<uint64_t>(len) * 8u;
    size_t off = 0;
    // Bloques completos.
    while (len - off >= 64) {
        sha256_block(in + off, h);
        off += 64;
    }
    // Padding: 0x80, ceros, longitud de 64 bits big-endian.
    uint8_t tail[128];
    const size_t rem = len - off;
    std::memcpy(tail, in + off, rem);
    tail[rem] = 0x80;
    size_t total = rem + 1;
    const size_t blocks = (total + 8 + 63) / 64;
    for (size_t i = total; i < blocks * 64; ++i) tail[i] = 0;
    for (int i = 0; i < 8; ++i) {
        tail[blocks * 64 - 1 - i] = static_cast<uint8_t>((bitlen >> (8 * i)) & 0xffu);
    }
    for (size_t i = 0; i < blocks; ++i) sha256_block(tail + i * 64, h);
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xffu);
        out[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xffu);
        out[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xffu);
        out[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xffu);
    }
    return true;
}

void integrity_sha256_hex(const uint8_t digest[32], char out[65]) {
    static const char* kHex = "0123456789abcdef";
    if (!digest || !out) return;
    for (int i = 0; i < 32; ++i) {
        out[i * 2] = kHex[(digest[i] >> 4) & 0xfu];
        out[i * 2 + 1] = kHex[digest[i] & 0xfu];
    }
    out[64] = '\0';
}

// ── SHA-256 por streaming (fase 2: hashear archivos grandes) ──────────────
// Reutiliza sha256_block (misma compresión que integrity_sha256); solo
// cambia el driver (por bloques en vez de una sola pasada). Verificado
// contra los mismos vectores NIST que integrity_sha256.
struct Sha256Stream {
    uint32_t h[8];
    uint64_t total_len;
    uint8_t buf[64];
    size_t buf_len;
};

void sha256_stream_init(Sha256Stream* ctx) {
    ctx->h[0] = 0x6a09e667u; ctx->h[1] = 0xbb67ae85u;
    ctx->h[2] = 0x3c6ef372u; ctx->h[3] = 0xa54ff53au;
    ctx->h[4] = 0x510e527fu; ctx->h[5] = 0x9b05688cu;
    ctx->h[6] = 0x1f83d9abu; ctx->h[7] = 0x5be0cd19u;
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void sha256_stream_update(Sha256Stream* ctx, const uint8_t* data, size_t len) {
    ctx->total_len += len;
    while (len > 0) {
        size_t take = 64 - ctx->buf_len;
        if (take > len) take = len;
        std::memcpy(ctx->buf + ctx->buf_len, data, take);
        ctx->buf_len += take;
        data += take;
        len -= take;
        if (ctx->buf_len == 64) {
            sha256_block(ctx->buf, ctx->h);
            ctx->buf_len = 0;
        }
    }
}

void sha256_stream_final(Sha256Stream* ctx, uint8_t out[32]) {
    const uint64_t bitlen = ctx->total_len * 8u;
    const uint8_t pad = 0x80;
    sha256_stream_update(ctx, &pad, 1);
    const uint8_t zero = 0;
    while (ctx->buf_len != 56) sha256_stream_update(ctx, &zero, 1);
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; ++i) {
        lenbuf[i] = static_cast<uint8_t>((bitlen >> (56 - 8 * i)) & 0xffu);
    }
    sha256_stream_update(ctx, lenbuf, 8);
    for (int i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<uint8_t>((ctx->h[i] >> 24) & 0xffu);
        out[i * 4 + 1] = static_cast<uint8_t>((ctx->h[i] >> 16) & 0xffu);
        out[i * 4 + 2] = static_cast<uint8_t>((ctx->h[i] >> 8) & 0xffu);
        out[i * 4 + 3] = static_cast<uint8_t>(ctx->h[i] & 0xffu);
    }
}

bool integrity_sha256_file(const char* path, char out_hex[65]) {
    if (!path || !out_hex) return false;
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    Sha256Stream ctx;
    sha256_stream_init(&ctx);
    uint8_t buf[65536];
    size_t n = 0;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        sha256_stream_update(&ctx, buf, n);
    }
    if (std::ferror(f)) ok = false;
    std::fclose(f);
    if (!ok) return false;
    uint8_t digest[32];
    sha256_stream_final(&ctx, digest);
    integrity_sha256_hex(digest, out_hex);
    return true;
}

bool integrity_self_check() {
    int cached = g_self_check_cached.load(std::memory_order_acquire);
    if (cached >= 0) return cached == 1;
#if defined(__ANDROID__)
    // Ruta desde la que se cargó ESTA .so (dladdr sobre una función propia).
    Dl_info info{};
    bool ok = false;
    if (dladdr(reinterpret_cast<void*>(&integrity_self_check), &info) != 0 &&
        info.dli_fname != nullptr) {
        const char* path = info.dli_fname;
        // Rutas legítimas: APK/lib de la app y particiones de sistema/vendor.
        // Cifradas en tiempo de compilación (fase 2): no aparecen en claro
        // en .rodata. Se descifran en pila, se usan y se borran.
        char p_app[16], p_vendor[16], p_system[16], p_odm[16], p_product[16];
        IVANNA_XSTR("/data/app/").decrypt(p_app, sizeof(p_app));
        IVANNA_XSTR("/vendor/").decrypt(p_vendor, sizeof(p_vendor));
        IVANNA_XSTR("/system/").decrypt(p_system, sizeof(p_system));
        IVANNA_XSTR("/odm/").decrypt(p_odm, sizeof(p_odm));
        IVANNA_XSTR("/product/").decrypt(p_product, sizeof(p_product));
        ok = starts_with(path, p_app) || starts_with(path, p_vendor) ||
             starts_with(path, p_system) || starts_with(path, p_odm) ||
             starts_with(path, p_product);
        obf::wipe(p_app, sizeof(p_app));
        obf::wipe(p_vendor, sizeof(p_vendor));
        obf::wipe(p_system, sizeof(p_system));
        obf::wipe(p_odm, sizeof(p_odm));
        obf::wipe(p_product, sizeof(p_product));
        if (!ok) {
            IVANNA_SEC_LOG("self-check: ruta de carga sospechosa: %s", path);
        }
    } else {
        // Sin dladdr no se puede verificar -> sospechoso (fail-closed).
        IVANNA_SEC_LOG("self-check: dladdr sin resultado");
        ok = false;
    }
#else
    // Fuera de Android (host/tests): no hay ruta de carga que verificar.
    // Se considera legítimo para no romper builds de host.
    bool ok = true;
#endif
    g_self_check_cached.store(ok ? 1 : 0, std::memory_order_release);
    return ok;
}

bool integrity_gate_ok() { return g_gate.load(std::memory_order_acquire); }

void integrity_set_gate(bool ok) { g_gate.store(ok, std::memory_order_release); }

void integrity_lockdown() {
    g_gate.store(false, std::memory_order_release);
    IVANNA_SEC_LOG("lockdown: gate de integridad a false (DSP en passthrough)");
}

}  // namespace ivanna::security
