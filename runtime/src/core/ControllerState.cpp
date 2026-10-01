#include "core/ControllerState.h"

#include <algorithm>
#include <cmath>

namespace pilotfly {

float clampAxis(float value) {
    if (std::isnan(value)) {
        return 0.0f;
    }
    return std::clamp(value, -1.0f, 1.0f);
}

int axisToEdgeTx(float value) {
    return static_cast<int>(axisToRange(value, 0, kEdgeTxAxisMax));
}

long axisToRange(float value, long rangeMin, long rangeMax) {
    const int step = static_cast<int>(std::lround((clampAxis(value) + 1.0f) * 0.5f * kEdgeTxAxisMax));
    const double fraction = static_cast<double>(step) / kEdgeTxAxisMax;
    return rangeMin + std::lround(fraction * static_cast<double>(rangeMax - rangeMin));
}

ControllerState fromChannels(const ChannelValues& channels) {
    ControllerState state;
    for (int i = 0; i < kAxisCount; ++i) {
        state.axes[i] = clampAxis(channels[i]);
    }
    for (int i = 0; i < kButtonCount; ++i) {
        state.buttons[i] = channels[kAxisCount + i] > 0.0f;
    }
    return state;
}

}
