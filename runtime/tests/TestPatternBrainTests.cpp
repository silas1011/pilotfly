#include <catch2/catch_test_macros.hpp>

#include <set>

#include "brain/TestPatternBrain.h"

using namespace pilotfly;

TEST_CASE("the test pattern touches every channel") {
    const Tx12Layout& layout = tx12Layout();
    TestPatternBrain brain(layout, 1.0);
    REQUIRE(brain.load().ready);
    brain.reset();

    std::set<int> movedAxes;
    std::set<int> pressedButtons;
    for (int i = 0; i < 32 * 20; ++i) {
        const ControllerState state = fromChannels(brain.step(0.05));
        for (int axis = 0; axis < kAxisCount; ++axis) {
            const float rest = axis == layout.throttleAxis ? -1.0f : 0.0f;
            if (state.axes[axis] > rest + 0.5f) {
                movedAxes.insert(axis);
            }
        }
        for (int button = 0; button < kButtonCount; ++button) {
            if (state.buttons[button]) {
                pressedButtons.insert(button);
            }
        }
    }

    REQUIRE(static_cast<int>(movedAxes.size()) == kAxisCount);
    REQUIRE(static_cast<int>(pressedButtons.size()) == kButtonCount);
}

TEST_CASE("the test pattern presses one button at a time") {
    TestPatternBrain brain(tx12Layout(), 1.0);
    brain.reset();
    for (int i = 0; i < 32 * 20; ++i) {
        const ControllerState state = fromChannels(brain.step(0.05));
        int pressed = 0;
        for (bool button : state.buttons) {
            pressed += button ? 1 : 0;
        }
        REQUIRE(pressed <= 1);
    }
}

TEST_CASE("reset starts the pattern again") {
    TestPatternBrain brain(tx12Layout(), 1.0);
    brain.reset();
    for (int i = 0; i < 50; ++i) {
        brain.step(0.05);
    }
    REQUIRE(brain.activeChannel() == 2);
    brain.reset();
    REQUIRE(brain.activeChannel() == 0);
}
