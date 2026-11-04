#include "moto_g85/ivanna_pal_asrc_write_stage.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace {

using Stage = ivanna::audio::moto_g85::PalAsrcWriteStage;
using ExpectedWriteBytes = int64_t (*)(void*, const void*, size_t) noexcept;

// Keep the adapter callback ABI honest: PAL write reports bytes or a negative
// error, not a boolean/synthetic status code.
static_assert(std::is_same_v<Stage::WriteBytes, ExpectedWriteBytes>);

TEST(MotoG85PalAsrcWriteStage, HeaderAndPalWriteContractCompile) {
    SUCCEED();
}

}  // namespace
