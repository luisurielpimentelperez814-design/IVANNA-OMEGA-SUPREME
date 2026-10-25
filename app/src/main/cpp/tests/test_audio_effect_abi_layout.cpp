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

static_assert(offsetof(audio_effect_library_t, query_num_effects) == 2 * sizeof(uint32_t) + 2 * sizeof(void*));
static_assert(offsetof(audio_effect_library_t, query_effect) == 2 * sizeof(uint32_t) + 3 * sizeof(void*));
static_assert(offsetof(audio_effect_library_t, create_effect) == 2 * sizeof(uint32_t) + 4 * sizeof(void*));
static_assert(offsetof(audio_effect_library_t, release_effect) == 2 * sizeof(uint32_t) + 5 * sizeof(void*));
static_assert(offsetof(audio_effect_library_t, get_descriptor) == 2 * sizeof(uint32_t) + 6 * sizeof(void*));

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

TEST(AudioEffectAbiLayoutTest, VtableSlotsRemainAospOrdered) {
    EXPECT_EQ(offsetof(effect_interface_s, process), 0u);
    EXPECT_EQ(offsetof(effect_interface_s, command), sizeof(void*));
    EXPECT_EQ(offsetof(effect_interface_s, get_descriptor), 2u * sizeof(void*));
    EXPECT_EQ(offsetof(effect_interface_s, process_reverse), 3u * sizeof(void*));
}
