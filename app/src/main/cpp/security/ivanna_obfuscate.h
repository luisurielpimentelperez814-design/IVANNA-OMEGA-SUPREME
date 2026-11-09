// IVANNA OMEGA SUPREME — Ofuscación de strings en tiempo de compilación.
// CONFIDENCIAL (interno). Ver docs/SECURITY_INTERNAL.md.
//
// Propósito: que los literales sensibles (rutas legítimas, nombres de
// frameworks de hook, artefactos de depuración) NO aparezcan en claro en
// .rodata de las .so. El string se guarda cifrado (XOR) y solo se descifra
// en runtime en un buffer de pila, se usa y se borra. Esto eleva el coste
// del análisis estático (`strings`/grep ya no revelan la lógica); NO es
// cifrado real (la clave está en el binario) y NO resiste análisis dinámico
// (un depurador ve el buffer en claro). Es una capa más, no una garantía.
//
// Anti-optimización: decrypt() es noinline y lee los bytes cifrados a
// través de un puntero volatile, para que el compilador no pueda
// constant-fold el literal en claro de vuelta a .rodata. Verificado con
// `strings` sobre el objeto compilado (ver tests/test_security_phase2.cpp).
//
// Uso:
//   char buf[32];
//   IVANNA_XSTR("/data/app/").decrypt(buf, sizeof(buf));
//   ... usar buf ...
//   ivanna::security::obf::wipe(buf, sizeof(buf));  // borrar tras usar
#pragma once

#include <cstddef>
#include <cstdint>

namespace ivanna::security::obf {

// String cifrado con XOR en tiempo de compilación.
// N incluye el NUL terminador. El constructor es constexpr: los bytes
// cifrados se calculan en tiempo de compilación. IMPORTANTE: para que el
// literal en claro NO se materialice en .rodata, el objeto debe ser
// `static constexpr` (ver IVANNA_XSTR): un temporal o una variable local
// normal NO se pliega y el compilador emite el literal en claro en
// .rodata.str1.1 (verificado con `strings`). Con `static constexpr` el
// cifrado se evalúa en compilación y solo los bytes cifrados van a .rodata.
template <std::size_t N>
class XorString {
public:
    // data_{} en la lista de inicialización: sin él el constructor NO es
    // constexpr válido (el miembro debe inicializarse por mem-initializer).
    // El orden es el de declaración (data_ antes que key_) para no generar
    // -Wreorder.
    constexpr XorString(const char (&s)[N], std::uint32_t seed)
        : data_{}, key_(seed) {
        for (std::size_t i = 0; i < N; ++i) {
            data_[i] = static_cast<char>(s[i] ^ key_byte(i));
        }
    }

    // Descifra en `out` (capacidad >= N, incluye NUL). Retorna out o nullptr.
    // noinline + lectura volatile: impide que el compilador pliegue el
    // literal en claro de vuelta a .rodata.
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((noinline))
#endif
    char* decrypt(char* out, std::size_t out_sz) const {
        if (!out || out_sz < N) return nullptr;
        const volatile char* src = data_;
        for (std::size_t i = 0; i < N; ++i) {
            out[i] = static_cast<char>(src[i] ^ key_byte(i));
        }
        return out;
    }

    static constexpr std::size_t size() { return N; }

private:
    constexpr std::uint8_t key_byte(std::size_t i) const {
        // Clave rotativa: byte de la semilla según la posición + índice,
        // para que no haya un keystream repetitivo.
        return static_cast<std::uint8_t>((key_ >> ((i % 4u) * 8u)) ^ (i * 0x5du) ^ 0xA5u);
    }

    char data_[N];
    std::uint32_t key_;
};

// Clave por sitio de uso: semilla fija mezclada con el número de línea, así
// el mismo literal se cifra distinto en cada sitio (cada uso tiene su clave).
#define IVANNA_XSTR_SEED \
    (0xA5C33C5Au ^ (static_cast<std::uint32_t>(__LINE__) * 0x9E3779B1u))

// Crea una referencia a un XorString `static constexpr` (una por sitio de
// uso). El `static constexpr` FUERZA la evaluación en tiempo de compilación:
// el cifrado ocurre al compilar y el literal en claro nunca se emite en
// .rodata (un temporal directo NO se pliega; verificado con `strings`).
// Se implementa con una lambda invocada de inmediato que devuelve una
// referencia al estático; el objeto es inmutable tras construirse, así que
// compartirlo entre llamadas es seguro. Uso: IVANNA_XSTR("texto").decrypt(...)
// o como argumento const XorString<N>&.
#define IVANNA_XSTR(literal) \
    ([]() -> const ::ivanna::security::obf::XorString<sizeof(literal)>& { \
        static constexpr ::ivanna::security::obf::XorString<sizeof(literal)> s( \
            literal, IVANNA_XSTR_SEED); \
        return s; \
    }())

// Borra un buffer de pila (evita que el literal descifrado quede en memoria).
inline void wipe(char* p, std::size_t n) {
    if (!p) return;
    volatile char* v = p;
    for (std::size_t i = 0; i < n; ++i) v[i] = 0;
}

// Predicados opacos: siempre verdaderos/falsos por aritmética modular
// (x^2 + x es par para todo entero x), pero el compilador no los pliega sin
// demostrarlo. Guardan código señuelo (bogus control flow) que se ejecuta
// pero cuyos resultados se descartan.
inline bool opaque_always_true(int x) {
    const long long v = static_cast<long long>(x);
    return ((v * v + v) & 1LL) == 0;
}

// (x^2 + x + 1) es impar para todo entero x.
inline bool opaque_always_false(int x) {
    const long long v = static_cast<long long>(x);
    return ((v * v + v + 1) & 1LL) == 0;
}

}  // namespace ivanna::security::obf
