#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "brain/IBrain.h"
#include "controller/IControllerSink.h"
#include "core/ControllerState.h"
#include "core/Tx12Layout.h"
#include "input/IStopKey.h"

namespace pilotfly {

struct RunSnapshot {
    ControllerState state;
    bool running = false;
    bool held = false;
    bool sinkReady = false;
    bool brainReady = false;
    double measuredHz = 0.0;
    double targetHz = 0.0;
    std::string sinkName;
    std::string sinkMessage;
    std::string brainName;
    std::string brainMessage;
};

class RunLoop {
public:
    RunLoop(IBrain& brain, IControllerSink& sink, const Tx12Layout& layout, double rateHz);
    ~RunLoop();

    RunLoop(const RunLoop&) = delete;
    RunLoop& operator=(const RunLoop&) = delete;

    void setWorkerStopKey(IStopKey* stopKey);
    void checkStatus();
    bool start();
    void stop();
    void toggleHold();
    RunSnapshot snapshot() const;

private:
    void run();

    IBrain& brain_;
    IControllerSink& sink_;
    const Tx12Layout& layout_;
    double rateHz_;
    IStopKey* workerStopKey_ = nullptr;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> held_{false};

    mutable std::mutex mutex_;
    RunSnapshot snapshot_;
};

}
