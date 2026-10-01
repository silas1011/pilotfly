#pragma once

#include <string>
#include <vector>

#include "core/ControllerState.h"

namespace pilotfly {

enum class SwitchRole { None, Arm, FlightMode };

struct StickSpec {
    std::string name;
    std::string labelX;
    std::string labelY;
    int axisX;
    int axisY;
    float x;
    float y;
};

struct SwitchSpec {
    std::string name;
    SwitchRole role;
    int positions;
    bool momentary;
    int axis;
    std::vector<int> buttons;
    float x;
    float y;
};

struct DialSpec {
    std::string name;
    int axis;
    float x;
    float y;
};

struct TrimSpec {
    std::string name;
    int buttonLow;
    int buttonHigh;
    bool horizontal;
    float x;
    float y;
};

struct Tx12Layout {
    std::vector<StickSpec> sticks;
    std::vector<SwitchSpec> switches;
    std::vector<DialSpec> dials;
    std::vector<TrimSpec> trims;
    std::vector<int> spareButtons;
    int throttleAxis;
};

const Tx12Layout& tx12Layout();
int switchPosition(const SwitchSpec& spec, const ControllerState& state);

}
