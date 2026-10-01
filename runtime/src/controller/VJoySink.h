#pragma once

#include <array>
#include <mutex>

#include "controller/IControllerSink.h"

namespace pilotfly {

class VJoySink : public IControllerSink {
public:
    explicit VJoySink(int deviceId);
    ~VJoySink() override;

    VJoySink(const VJoySink&) = delete;
    VJoySink& operator=(const VJoySink&) = delete;

    std::string name() const override;
    SinkStatus open() override;
    void close() override;
    bool send(const ControllerState& state) override;

private:
    SinkStatus fail(const std::string& message);
    void release();

    mutable std::mutex mutex_;
    int deviceId_;
    bool acquired_ = false;
    bool open_ = false;
    std::array<long, kAxisCount> axisMin_{};
    std::array<long, kAxisCount> axisMax_{};
};

}
