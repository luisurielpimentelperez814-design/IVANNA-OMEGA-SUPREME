#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include "include/gauge_field_engine.hpp"

namespace {
using ivanna::gfe::BerryPhaseProjector;
using ivanna::gfe::Complex;
using ivanna::gfe::Fir128;
using ivanna::gfe::State;
}

TEST(GaugeFieldEngine, RejectsNonFiniteAndZeroStatesWithoutAdvancing) {
    BerryPhaseProjector projector;
    State state{};
    state[0] = Complex(1.0f, 0.0f);
    ASSERT_TRUE(projector.observe(state).valid);
    const double before = projector.phase();
    state[0] = Complex(std::numeric_limits<float>::quiet_NaN(), 0.0f);
    EXPECT_TRUE(projector.observe(state).valid);
    EXPECT_DOUBLE_EQ(projector.phase(), before);
    state.fill(Complex{});
    EXPECT_TRUE(projector.observe(state).valid);
    EXPECT_DOUBLE_EQ(projector.phase(), before);
}

TEST(GaugeFieldEngine, ConstantStateHasZeroGeometricIncrement) {
    BerryPhaseProjector projector;
    State state{};
    state[0] = Complex(1.0f, 0.0f);
    ASSERT_TRUE(projector.observe(state).valid);
    ASSERT_TRUE(projector.observe(state).valid);
    EXPECT_NEAR(projector.phase(), 0.0, 1e-9);
    const Complex result = projector.project(Complex(1.0f, 0.0f));
    EXPECT_NEAR(result.real(), 1.0, 2e-5);
    EXPECT_NEAR(result.imag(), 0.0, 2e-5);
}

TEST(GaugeFieldEngine, Fir128ImpulseUsesCallerProvidedTaps) {
    Fir128::Taps taps{};
    taps[0] = Complex(0.5f, 0.0f);
    taps[1] = Complex(0.25f, 0.0f);
    Fir128 fir(taps);
    EXPECT_NEAR(fir.process(Complex(1.0f, 0.0f)).real(), 0.5, 1e-6);
    EXPECT_NEAR(fir.process(Complex(0.0f, 0.0f)).real(), 0.25, 1e-6);
}

TEST(GaugeFieldEngine, RejectsNonFiniteInputInFir) {
    Fir128::Taps taps{};
    taps[0] = Complex(1.0f, 0.0f);
    Fir128 fir(taps);
    const auto out = fir.process(Complex(std::numeric_limits<float>::infinity(), 0.0f));
    EXPECT_FLOAT_EQ(out.real(), 0.0f);
    EXPECT_FLOAT_EQ(out.imag(), 0.0f);
}
