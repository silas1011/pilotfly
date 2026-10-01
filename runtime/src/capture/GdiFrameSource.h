#pragma once

#include <cstdint>

#include "capture/IFrameSource.h"

namespace pilotfly {

class GdiFrameSource : public IFrameSource {
public:
    explicit GdiFrameSource(std::string windowTitle = "Uncrashed");

    std::string name() const override;
    FrameStatus open() override;
    void close() override;
    bool grab(std::vector<float>& gray, int width, int height) override;

private:
    bool findWindow();

    std::string windowTitle_;
    void* window_ = nullptr;
    std::vector<std::uint8_t> pixels_;
};

}
