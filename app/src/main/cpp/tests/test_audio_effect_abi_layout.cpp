// ABI regression tests against AOSP hardware/libhardware/include/hardware/audio_effect.h.
// effect_descriptor_t is consumed across the audioserver dlopen boundary: field widths
// affect every following field, so checking only constants is insufficient.
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include "gtest/gtest.h"
#include "../include/audio_effect_compat.h"

static_assert(sizeof(effect_uuid_t) == 16);
static_assert(sizeof(effect_descriptor_t) == 172,
              "AOSP effect_descriptor_t must be 172 bytes on 32/64-bit Android ABIs");
static_assert(offsetof(effect_descriptor_t, apiVersion) == 32);
static_assert(offsetof(effect_descriptor_t, flags) == 36);
static_assert(offsetof(effect_descriptor_t, cpuLoad) == 40);
static_assert(offsetof(effect_descriptor_t, memoryUsage) == 42);
static_assert(offsetof(effect_descriptor_t, name) == 44);
static_assert(offsetof(effect_descriptor_t, implementor) == 108);
static_assert(std::is_same_v<decltype(effect_descriptor_t::cpuLoad), uint16_t>);
static_assert(std::is_same_v<decltype(effect_descriptor_t::memoryUsage), uint16_t>);

// The effect vtable and library table are ABI function-pointer arrays in practice.
// These assertions guard the order/stride so callbacks cannot silently shift slots.
static_assert(offsetof(effect_interface_s, process) == 0);
static_assert(offsetof(effect_interface_s, command) == sizeof(void*));
static_assert(offsetof(effect_interface_s, get_descriptor) == 2 * sizeof(void*));
static_assert(offsetof(effect_interface_s, process_reverse) == 3 * sizeof(void*));
static_assert(sizeof(effect_interface_s) == 4 * sizeof(void*));

static_assert(offsetof(audio_effect_library_t, tag) == 0);
static_assert(offsetof(audio_effect_library_t, version) == sizeof(uint32_t));
static_assert(offsetof(audio_effect_library_t, name) == 2 * sizeof(uint32_t));
static_assert(offsetof(audio_effect_library_t, implementor) == 2 * sizeof(uint32_t) + sizeof(void*));
static_assert(offsetof(audio_effect_library_t, create_effect) == 2 * sizeof(uint32_t) + 2 * sizeof(void*));
static_assert(offsetof(audio_effect_library_t, release_effect) == 2 * sizeof(uint32_t) + 3 * sizeof(void*));
static_assert(offsetof(audio_effect_library_t, get_descriptor) == 2 * sizeof(uint32_t) + 4 * sizeof(void*));
static_assert(sizeof(audio_effect_library_t) == 2 * sizeof(uint32_t) + 5 * sizeof(void*));

TEST(AudioEffectAbiLayoutTest, DescriptorMatchesAospLayout) {
    effect_descriptor_t descriptor{};
    descriptor.cpuLoad = static_cast<uint16_t>(100);
    descriptor.memoryUsage = static_cast<uint16_t>(1024);
    descriptor.name[0] = 'I';
    descriptor.implementor[0] = 'I';

    const auto* bytes = reinterpret_cast<const uint8_t*>(&descriptor);
    EXPECT_EQ(bytes[offsetof(effect_descriptor_t, name)], static_cast<uint8_t>('I'));
    EXPECT_EQ(bytes[offsetof(effect_descriptor_t, implementor)], static_cast<uint8_t>('I'));
    EXPECT_EQ(descriptor.cpuLoad, 100u);
    EXPECT_EQ(descriptor.memoryUsage, 1024u);
}

// v2.5.374 — Los valores de flags están fijados en audio_effect_compat.h
// (static_asserts). Este test los ancla en runtime contra el layout moderno
// (HIDL 4.0 / audio_effect-base.h): el layout legacy (VOLUME_CTRL 0x400 =
// INSERT_EXCLUSIVE moderno) ya no puede volver a colarse sin romper la build
// Y este test. También verifica que los flags del dumpsys del G85 se
// decodifican como corresponde (Equalizer 0x48 = INSERT/FIRST/VOLUME_CTRL).
TEST(AudioEffectAbiLayoutTest, FlagsMatchModernHidlLayout) {
    // El layout legacy tenía VOLUME_CTRL=0x400 y DEVICE_IND=0x800; en el
    // layout moderno 0x400 es INSERT_EXCLUSIVE y 0x800 es VOLUME_IND.
    EXPECT_EQ(EFFECT_FLAG_TYPE_INSERT, 0x00000000u);
    EXPECT_EQ(EFFECT_FLAG_INSERT_FIRST, 0x00000008u);
    EXPECT_EQ(EFFECT_FLAG_INSERT_LAST, 0x00000010u);
    EXPECT_EQ(EFFECT_FLAG_INSERT_EXCLUSIVE, 0x00000018u);
    EXPECT_EQ(EFFECT_FLAG_VOLUME_CTRL, 0x00000040u);
    EXPECT_EQ(EFFECT_FLAG_VOLUME_IND, 0x00000080u);
    EXPECT_EQ(EFFECT_FLAG_DEVICE_IND, 0x00000200u);
    EXPECT_EQ(EFFECT_FLAG_OFFLOAD_SUPPORTED, 0x00400000u);
    EXPECT_EQ(EFFECT_FLAG_NO_PROCESS, 0x00800000u);
    // Decodificación del dumpsys real del G85: Equalizer 0x48 =
    // INSERT(0) + FIRST(0x08) + VOLUME_CTRL(0x40).
    const uint32_t eqFlags = 0x48u;
    EXPECT_EQ(eqFlags & EFFECT_FLAG_TYPE_MASK, EFFECT_FLAG_TYPE_INSERT);
    EXPECT_EQ(eqFlags & EFFECT_FLAG_INSERT_MASK, EFFECT_FLAG_INSERT_FIRST);
    EXPECT_TRUE((eqFlags & EFFECT_FLAG_VOLUME_MASK) == EFFECT_FLAG_VOLUME_CTRL);
    // Omega reporta flags=0 (INSERT_ANY): no reclama exclusividad ni
    // volumen, así que no puede ser el origen del -38 (ENOSYS).
    EXPECT_EQ(0u & EFFECT_FLAG_INSERT_MASK, EFFECT_FLAG_INSERT_ANY);
}
