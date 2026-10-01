#pragma once

#include <string>

#include "core/ControllerState.h"

namespace pilotfly {

struct BrainStatus {
    bool ready = false;
    std::string message;
};

class IBrain {
public:
    virtual ~IBrain() = default;
    virtual std::string name() const = 0;
    virtual BrainStatus load() = 0;
    virtual void reset() = 0;
    virtual ChannelValues step(double dtSeconds) = 0;
};

}
