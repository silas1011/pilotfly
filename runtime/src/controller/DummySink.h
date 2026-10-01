#pragma once

#include <mutex>

#include "controller/IControllerSink.h"

namespace pilotfly {

class DummySink : public IControllerSink {
public:
    std::string name() const override;
    SinkStatus open() override;
    void close() override;
    bool send(const ControllerState& state) override;

    bool isOpen() const;
    int sendCount() const;
    ControllerState lastState() const;

private:
    mutable std::mutex mutex_;
    bool open_ = false;
    int sendCount_ = 0;
    ControllerState last_;
};

}
