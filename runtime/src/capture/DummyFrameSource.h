#pragma once

#include "capture/IFrameSource.h"

namespace pilotfly {

class DummyFrameSource : public IFrameSource {
public:
    std::string name() const override;
    FrameStatus open() override;
    void close() override;
    bool grab(std::vector<float>& gray, int width, int height) override;

private:
    long long frame_ = 0;
};

}
