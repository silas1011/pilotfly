#pragma once

#include <memory>
#include <string>
#include <vector>

#include "brain/IBrain.h"
#include "capture/IFrameSource.h"

namespace pilotfly {

inline constexpr int kBrainFrameWidth = 128;
inline constexpr int kBrainFrameHeight = 64;
inline constexpr double kDefaultBrainRateHz = 50.0;

class OnnxBrain : public IBrain {
public:
    OnnxBrain(std::string modelPath, IFrameSource& frames);
    ~OnnxBrain() override;

    OnnxBrain(const OnnxBrain&) = delete;
    OnnxBrain& operator=(const OnnxBrain&) = delete;

    std::string name() const override;
    BrainStatus load() override;
    void reset() override;
    ChannelValues step(double dtSeconds) override;
    double preferredRateHz() const override;

    double rateHz() const;

private:
    struct Runtime;

    BrainStatus createRuntime();

    std::string modelPath_;
    IFrameSource& frames_;
    std::unique_ptr<Runtime> runtime_;
    std::string provider_;
    double rateHz_ = kDefaultBrainRateHz;
    std::vector<float> frame_;
    std::vector<float> grabbed_;
    std::vector<float> state_;
    ChannelValues lastChannels_ = {};
};

}
