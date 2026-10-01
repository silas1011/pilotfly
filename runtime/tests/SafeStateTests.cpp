#include <catch2/catch_test_macros.hpp>

#include "core/SafeState.h"

using namespace pilotfly;

TEST_CASE("the safe state disarms and cuts the throttle") {
    const Tx12Layout& layout = tx12Layout();
    const ControllerState state = safeState(layout);

    REQUIRE(state.axes[layout.throttleAxis] == -1.0f);
    for (const auto& stick : layout.sticks) {
        if (stick.axisX != layout.throttleAxis) {
            REQUIRE(state.axes[stick.axisX] == 0.0f);
        }
        if (stick.axisY != layout.throttleAxis) {
            REQUIRE(state.axes[stick.axisY] == 0.0f);
        }
    }

    bool foundArm = false;
    for (const auto& spec : layout.switches) {
        if (spec.role == SwitchRole::Arm) {
            foundArm = true;
            REQUIRE(switchPosition(spec, state) == 0);
        }
    }
    REQUIRE(foundArm);

    for (bool button : state.buttons) {
        REQUIRE_FALSE(button);
    }
}
