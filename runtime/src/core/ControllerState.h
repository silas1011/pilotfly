#pragma once

#include <array>

namespace pilotfly {

inline constexpr int kAxisCount = 8;
inline constexpr int kButtonCount = 24;
inline constexpr int kChannelCount = kAxisCount + kButtonCount;
inline constexpr int kEdgeTxAxisMax = 2047;

using ChannelValues = std::array<float, kChannelCount>;

struct ControllerState {
    std::array<float, kAxisCount> axes{};
    std::array<bool, kButtonCount> buttons{};

    bool operator==(const ControllerState&) const = default;
};

float clampAxis(float value);
int axisToEdgeTx(float value);
long axisToRange(float value, long rangeMin, long rangeMax);
ControllerState fromChannels(const ChannelValues& channels);

}
