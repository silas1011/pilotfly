#include <catch2/catch_test_macros.hpp>

#include <set>

#include "core/Tx12Layout.h"

using namespace pilotfly;

namespace {

const SwitchSpec& findSwitch(const std::string& name) {
    for (const auto& spec : tx12Layout().switches) {
        if (spec.name == name) {
            return spec;
        }
    }
    FAIL("switch not found: " << name);
    return tx12Layout().switches.front();
}

}

TEST_CASE("every axis and button belongs to exactly one control") {
    const Tx12Layout& layout = tx12Layout();
    std::multiset<int> axes;
    std::multiset<int> buttons;

    for (const auto& spec : layout.sticks) {
        axes.insert(spec.axisX);
        axes.insert(spec.axisY);
    }
    for (const auto& spec : layout.dials) {
        axes.insert(spec.axis);
    }
    for (const auto& spec : layout.switches) {
        if (spec.axis >= 0) {
            axes.insert(spec.axis);
            REQUIRE(spec.buttons.empty());
        } else {
            REQUIRE(static_cast<int>(spec.buttons.size()) == spec.positions - 1);
        }
        for (int button : spec.buttons) {
            buttons.insert(button);
        }
    }
    for (const auto& spec : layout.trims) {
        buttons.insert(spec.buttonLow);
        buttons.insert(spec.buttonHigh);
    }
    for (int button : layout.spareButtons) {
        buttons.insert(button);
    }

    REQUIRE(static_cast<int>(axes.size()) == kAxisCount);
    REQUIRE(static_cast<int>(buttons.size()) == kButtonCount);
    for (int i = 0; i < kAxisCount; ++i) {
        REQUIRE(axes.count(i) == 1);
    }
    for (int i = 0; i < kButtonCount; ++i) {
        REQUIRE(buttons.count(i) == 1);
    }
}

TEST_CASE("the layout matches the TX12 Mark II controls") {
    const Tx12Layout& layout = tx12Layout();
    REQUIRE(layout.sticks.size() == 2);
    REQUIRE(layout.switches.size() == 6);
    REQUIRE(layout.dials.size() == 2);
    REQUIRE(layout.trims.size() == 4);
    REQUIRE(layout.throttleAxis == 2);
    REQUIRE(findSwitch("SA").positions == 2);
    REQUIRE(findSwitch("SD").positions == 2);
    REQUIRE(findSwitch("SB").positions == 3);
    REQUIRE(findSwitch("SC").positions == 3);
    REQUIRE(findSwitch("SE").positions == 3);
    REQUIRE(findSwitch("SF").positions == 3);
}

TEST_CASE("controls the game cannot read are only spare buttons") {
    for (const auto& spec : tx12Layout().switches) {
        for (int button : spec.buttons) {
            REQUIRE(button < 20);
        }
    }
    for (const auto& spec : tx12Layout().trims) {
        REQUIRE(spec.buttonLow < 20);
        REQUIRE(spec.buttonHigh < 20);
    }
}

TEST_CASE("an axis switch has one position per level") {
    const SwitchSpec& mode = findSwitch("SB");
    ControllerState state;

    state.axes[mode.axis] = -1.0f;
    REQUIRE(switchPosition(mode, state) == 0);
    state.axes[mode.axis] = 0.1f;
    REQUIRE(switchPosition(mode, state) == 1);
    state.axes[mode.axis] = 0.9f;
    REQUIRE(switchPosition(mode, state) == 2);
}

TEST_CASE("a button switch takes the highest pressed button") {
    const SwitchSpec& sc = findSwitch("SC");
    ControllerState state;

    REQUIRE(switchPosition(sc, state) == 0);
    state.buttons[sc.buttons[0]] = true;
    REQUIRE(switchPosition(sc, state) == 1);
    state.buttons[sc.buttons[1]] = true;
    REQUIRE(switchPosition(sc, state) == 2);
    state.buttons[sc.buttons[0]] = false;
    REQUIRE(switchPosition(sc, state) == 2);
}
