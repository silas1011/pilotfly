#include "brain/TestPatternBrain.h"

#include <cmath>
#include <numbers>

namespace pilotfly {

TestPatternBrain::TestPatternBrain(const Tx12Layout& layout, double secondsPerChannel)
    : layout_(layout), secondsPerChannel_(secondsPerChannel) {}

std::string TestPatternBrain::name() const {
    return "Test pattern";
}

BrainStatus TestPatternBrain::load() {
    return {true, "Moves every control one after another, no fly brain yet"};
}

void TestPatternBrain::reset() {
    time_ = 0.0;
}

double TestPatternBrain::preferredRateHz() const {
    return 0.0;
}

int TestPatternBrain::activeChannel() const {
    return static_cast<int>(time_ / secondsPerChannel_) % kChannelCount;
}

ChannelValues TestPatternBrain::step(double dtSeconds) {
    ChannelValues channels;
    channels.fill(-1.0f);
    for (int i = 0; i < kAxisCount; ++i) {
        channels[i] = 0.0f;
    }
    channels[layout_.throttleAxis] = -1.0f;

    const int active = activeChannel();
    if (active < kAxisCount) {
        const double phase = std::fmod(time_, secondsPerChannel_) / secondsPerChannel_;
        channels[active] = static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));
    } else {
        channels[active] = 1.0f;
    }

    time_ += dtSeconds;
    return channels;
}

}
