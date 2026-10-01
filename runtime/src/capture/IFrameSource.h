#pragma once

#include <string>
#include <vector>

namespace pilotfly {

struct FrameStatus {
    bool ready = false;
    std::string message;
};

class IFrameSource {
public:
    virtual ~IFrameSource() = default;
    virtual std::string name() const = 0;
    virtual FrameStatus open() = 0;
    virtual void close() = 0;
    virtual bool grab(std::vector<float>& gray, int width, int height) = 0;
};

}
