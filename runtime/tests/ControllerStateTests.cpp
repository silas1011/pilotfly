#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "core/ControllerState.h"

using namespace pilotfly;

TEST_CASE("axis values are clamped to the stick range") {
    REQUIRE(clampAxis(2.0f) == 1.0f);
    REQUIRE(clampAxis(-3.0f) == -1.0f);
    REQUIRE(clampAxis(0.25f) == 0.25f);
    REQUIRE(clampAxis(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
}

TEST_CASE("axis values map to the 11 bit EdgeTX range") {
    REQUIRE(axisToEdgeTx(-1.0f) == 0);
    REQUIRE(axisToEdgeTx(1.0f) == 2047);
    REQUIRE(axisToEdgeTx(0.0f) == 1024);
    REQUIRE(axisToEdgeTx(5.0f) == 2047);
}

TEST_CASE("axis values map to a device range with 11 bit steps") {
    REQUIRE(axisToRange(-1.0f, 1, 32768) == 1);
    REQUIRE(axisToRange(1.0f, 1, 32768) == 32768);
    REQUIRE(axisToRange(0.0f, 0, 2047) == 1024);

    const long a = axisToRange(0.10000f, 1, 32768);
    const long b = axisToRange(0.10001f, 1, 32768);
    REQUIRE(a == b);
}

TEST_CASE("channels become axes and buttons") {
    ChannelValues channels{};
    channels[0] = 0.5f;
    channels[7] = -4.0f;
    channels[8] = 0.1f;
    channels[9] = 0.0f;
    channels[10] = -0.1f;
    channels[31] = 1.0f;

    const ControllerState state = fromChannels(channels);

    REQUIRE(state.axes[0] == 0.5f);
    REQUIRE(state.axes[7] == -1.0f);
    REQUIRE(state.buttons[0]);
    REQUIRE_FALSE(state.buttons[1]);
    REQUIRE_FALSE(state.buttons[2]);
    REQUIRE(state.buttons[23]);
}
