#pragma once

#include "brain/IBrain.h"
#include "core/Tx12Layout.h"

namespace pilotfly {

class TestPatternBrain : public IBrain {
public:
    explicit TestPatternBrain(const Tx12Layout& layout, double secondsPerChannel = 1.5);

    std::string name() const override;
    BrainStatus load() override;
    void reset() override;
    ChannelValues step(double dtSeconds) override;
    double preferredRateHz() const override;

    int activeChannel() const;

private:
    const Tx12Layout& layout_;
    double secondsPerChannel_;
    double time_ = 0.0;
};

}
