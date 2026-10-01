#pragma once

#include <array>

#include "core/ControllerState.h"
#include "core/Tx12Layout.h"

namespace pilotfly {

class Tx12View {
public:
    explicit Tx12View(const Tx12Layout& layout);

    void draw(const ControllerState& state, bool held, double nowSeconds);

private:
    void trackChanges(const ControllerState& state, double nowSeconds);
    float axisGlow(int axis, double nowSeconds) const;
    float buttonGlow(int button, double nowSeconds) const;
    float switchGlow(const SwitchSpec& spec, double nowSeconds) const;

    const Tx12Layout& layout_;
    ControllerState previous_;
    bool hasPrevious_ = false;
    std::array<double, kAxisCount> axisChangedAt_{};
    std::array<double, kButtonCount> buttonChangedAt_{};
};

}
