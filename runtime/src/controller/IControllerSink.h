#pragma once

#include <string>

#include "core/ControllerState.h"

namespace pilotfly {

struct SinkStatus {
    bool ready = false;
    std::string message;
};

class IControllerSink {
public:
    virtual ~IControllerSink() = default;
    virtual std::string name() const = 0;
    virtual SinkStatus open() = 0;
    virtual void close() = 0;
    virtual bool send(const ControllerState& state) = 0;
};

}
