#include "core/Tx12Layout.h"

#include <algorithm>
#include <cmath>

namespace pilotfly {

namespace {

Tx12Layout buildLayout() {
    Tx12Layout layout;
    layout.throttleAxis = 2;

    layout.sticks = {
        {"Left stick", "Yaw", "Throttle", 3, 2, 0.28f, 0.42f},
        {"Right stick", "Roll", "Pitch", 0, 1, 0.72f, 0.42f},
    };

    layout.switches = {
        {"SE", SwitchRole::Arm, 3, false, 4, {}, 0.10f, 0.07f},
        {"SA", SwitchRole::None, 2, true, -1, {0}, 0.12f, 0.20f},
        {"SB", SwitchRole::FlightMode, 3, false, 5, {}, 0.27f, 0.13f},
        {"SC", SwitchRole::None, 3, false, -1, {1, 2}, 0.73f, 0.13f},
        {"SD", SwitchRole::None, 2, true, -1, {3}, 0.88f, 0.20f},
        {"SF", SwitchRole::None, 3, false, -1, {4, 5}, 0.90f, 0.07f},
    };

    layout.dials = {
        {"S1", 6, 0.06f, 0.58f},
        {"S2", 7, 0.94f, 0.58f},
    };

    layout.trims = {
        {"Yaw trim", 6, 7, true, 0.36f, 0.68f},
        {"Throttle trim", 8, 9, false, 0.45f, 0.64f},
        {"Pitch trim", 10, 11, false, 0.55f, 0.64f},
        {"Roll trim", 12, 13, true, 0.64f, 0.68f},
    };

    std::vector<bool> used(kButtonCount, false);
    for (const auto& spec : layout.switches) {
        for (int button : spec.buttons) {
            used[button] = true;
        }
    }
    for (const auto& spec : layout.trims) {
        used[spec.buttonLow] = true;
        used[spec.buttonHigh] = true;
    }
    for (int i = 0; i < kButtonCount; ++i) {
        if (!used[i]) {
            layout.spareButtons.push_back(i);
        }
    }
    return layout;
}

}

const Tx12Layout& tx12Layout() {
    static const Tx12Layout layout = buildLayout();
    return layout;
}

int switchPosition(const SwitchSpec& spec, const ControllerState& state) {
    if (spec.axis >= 0) {
        const float fraction = (clampAxis(state.axes[spec.axis]) + 1.0f) * 0.5f;
        const int position = static_cast<int>(std::lround(fraction * static_cast<float>(spec.positions - 1)));
        return std::clamp(position, 0, spec.positions - 1);
    }
    int position = 0;
    for (int i = 0; i < static_cast<int>(spec.buttons.size()); ++i) {
        if (state.buttons[spec.buttons[i]]) {
            position = i + 1;
        }
    }
    return std::min(position, spec.positions - 1);
}

}
