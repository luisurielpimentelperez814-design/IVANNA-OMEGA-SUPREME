// Regresión del hallazgo "AUDIO_EFFECT_LIBRARY_TAG byte-swapped vs AOSP real"
// (v2.5.139). Sin este test, el bug sobrevivió indetectado porque ningún otro
// gate del repo compara el VALOR NUMÉRICO del tag contra el real de AOSP —
// check_jni_wiring/check_header_wiring verifican que los símbolos existan y
// estén enlazados, no que las constantes del ABI de efecto tengan el valor
// correcto. Este test fija ese valor explícitamente, citando la fuente
// primaria (hardware/libhardware/include/hardware/audio_effect.h real de
// AOSP), para que un futuro cambio accidental al compat shim lo cace en CI
// en vez de fallar en silencio en audioserver con "Bad tag ... expected ...".
#include <cstdint>
#include "gtest/gtest.h"
#include "../include/audio_effect_compat.h"

TEST(AudioEffectLibraryTagTest, CoincideConElValorRealDeAOSP) {
    // Fuente primaria (frameworks/av EffectsXmlConfigLoader.cpp::loadLibrary()
    // via hardware/libhardware/include/hardware/audio_effect.h):
    //   #define AUDIO_EFFECT_LIBRARY_TAG \
    //       ((('A') << 24) | (('E') << 16) | (('L') << 8) | ('T'))
    constexpr uint32_t kAospRealTag =
        ((static_cast<uint32_t>('A') << 24) | (static_cast<uint32_t>('E') << 16) |
         (static_cast<uint32_t>('L') << 8) | static_cast<uint32_t>('T'));

    EXPECT_EQ(static_cast<uint32_t>(AUDIO_EFFECT_LIBRARY_TAG), kAospRealTag)
        << "AUDIO_EFFECT_LIBRARY_TAG no coincide con el valor real de AOSP. "
        << "Con un valor distinto, EffectsXmlConfigLoader::loadLibrary() del "
        << "audioserver REAL rechaza la librería en el primer chequeo tras "
        << "dlsym(\"AELI\") — antes de audio_effects.xml, antes de SELinux, "
        << "antes de cualquier otra cosa. Valor esperado: 0x41454C54.";

    // Regresión explícita del valor concreto que causó el bug real: si
    // alguien revierte el fix a los shifts invertidos, este assert lo
    // detecta incluso si alguien también "corrige" kAospRealTag arriba por
    // error.
    EXPECT_EQ(static_cast<uint32_t>(AUDIO_EFFECT_LIBRARY_TAG), 0x41454C54u);
    EXPECT_NE(static_cast<uint32_t>(AUDIO_EFFECT_LIBRARY_TAG), 0x544C4541u)
        << "Regresó al valor byte-swapped (bug original, v2.5.138 y anteriores).";
}

TEST(AudioEffectLibraryTagTest, VersionesDeApiCoincidenConAOSP) {
    // EFFECT_MAKE_API_VERSION(M,m) = ((M)<<16) | ((m)&0xFFFF) — verificado
    // igual que el real de AOSP; estas dos sí coincidían ya antes del fix,
    // se fijan aquí para que sigan coincidiendo.
    EXPECT_EQ(static_cast<uint32_t>(EFFECT_LIBRARY_API_VERSION), 0x00030000u);
    EXPECT_EQ(static_cast<uint32_t>(EFFECT_CONTROL_API_VERSION), 0x00020000u);
}
