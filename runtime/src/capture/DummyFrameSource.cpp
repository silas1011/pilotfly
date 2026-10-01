#include "capture/DummyFrameSource.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace pilotfly {

namespace {

constexpr double kStripes = 3.0;
constexpr double kFramesPerCycle = 200.0;

}

std::string DummyFrameSource::name() const {
    return "Dummy picture";
}

FrameStatus DummyFrameSource::open() {
    return {true, "Dummy picture (moving stripes), the game is not captured on this system"};
}

void DummyFrameSource::close() {}

bool DummyFrameSource::grab(std::vector<float>& gray, int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    gray.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const double shift = static_cast<double>(frame_) / kFramesPerCycle;
    for (int y = 0; y < height; ++y) {
        const double fade = 0.5 + 0.5 * static_cast<double>(y) / static_cast<double>(height);
        for (int x = 0; x < width; ++x) {
            const double position = static_cast<double>(x) / static_cast<double>(width);
            const double wave = std::sin(2.0 * std::numbers::pi * (position * kStripes + shift));
            const double value = (0.5 + 0.5 * wave) * fade;
            gray[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                static_cast<float>(std::clamp(value, 0.0, 1.0));
        }
    }
    ++frame_;
    return true;
}

}
