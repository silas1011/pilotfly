#include "app/RunLoop.h"

#include <chrono>

#include "core/SafeState.h"

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

namespace pilotfly {

namespace {

class TimerResolution {
public:
    TimerResolution() {
#ifdef _WIN32
        timeBeginPeriod(1);
#endif
    }
    ~TimerResolution() {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }
    TimerResolution(const TimerResolution&) = delete;
    TimerResolution& operator=(const TimerResolution&) = delete;
};

}

RunLoop::RunLoop(IBrain& brain, IControllerSink& sink, const Tx12Layout& layout, double rateHz)
    : brain_(brain), sink_(sink), layout_(layout), rateHz_(rateHz) {
    snapshot_.state = safeState(layout_);
    snapshot_.targetHz = rateHz_;
    snapshot_.sinkName = sink_.name();
    snapshot_.brainName = brain_.name();
}

RunLoop::~RunLoop() {
    stop();
}

void RunLoop::setWorkerStopKey(IStopKey* stopKey) {
    if (running_) {
        return;
    }
    workerStopKey_ = stopKey;
}

void RunLoop::checkStatus() {
    if (running_) {
        return;
    }
    const SinkStatus sinkStatus = sink_.open();
    sink_.close();
    const BrainStatus brainStatus = brain_.load();
    std::lock_guard lock(mutex_);
    snapshot_.sinkReady = sinkStatus.ready;
    snapshot_.sinkMessage = sinkStatus.message;
    snapshot_.brainReady = brainStatus.ready;
    snapshot_.brainMessage = brainStatus.message;
}

bool RunLoop::start() {
    if (running_) {
        return true;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    const BrainStatus brainStatus = brain_.load();
    SinkStatus sinkStatus;
    if (brainStatus.ready) {
        sinkStatus = sink_.open();
    } else {
        sinkStatus = sink_.open();
        sink_.close();
    }
    const bool ready = brainStatus.ready && sinkStatus.ready;
    {
        std::lock_guard lock(mutex_);
        snapshot_.sinkReady = sinkStatus.ready;
        snapshot_.sinkMessage = sinkStatus.message;
        snapshot_.brainReady = brainStatus.ready;
        snapshot_.brainMessage = brainStatus.message;
        snapshot_.running = ready;
        snapshot_.measuredHz = 0.0;
    }
    if (!ready) {
        if (sinkStatus.ready) {
            sink_.close();
        }
        return false;
    }
    brain_.reset();
    held_ = false;
    running_ = true;
    thread_ = std::thread(&RunLoop::run, this);
    return true;
}

void RunLoop::stop() {
    const bool wasRunning = running_.exchange(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    if (!wasRunning) {
        return;
    }
    const ControllerState safe = safeState(layout_);
    const bool sent = sink_.send(safe);
    sink_.close();
    held_ = false;
    std::lock_guard lock(mutex_);
    snapshot_.state = safe;
    snapshot_.running = false;
    snapshot_.measuredHz = 0.0;
    if (!sent) {
        snapshot_.sinkReady = false;
        snapshot_.sinkMessage = "Could not send the safe state when stopping. The controller may still hold the last values.";
    }
}

void RunLoop::toggleHold() {
    if (!running_) {
        return;
    }
    held_ = !held_.load();
}

RunSnapshot RunLoop::snapshot() const {
    std::lock_guard lock(mutex_);
    RunSnapshot copy = snapshot_;
    copy.held = copy.running && held_;
    return copy;
}

void RunLoop::run() {
    using Clock = std::chrono::steady_clock;
    const TimerResolution timerResolution;
    const std::string sinkMessage = snapshot().sinkMessage;
    const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / rateHz_));
    const ControllerState safe = safeState(layout_);
    auto previous = Clock::now();
    auto next = previous + period;
    double measuredHz = rateHz_;
    double dt = 1.0 / rateHz_;

    while (running_) {
        if (workerStopKey_ && workerStopKey_->consumePress()) {
            held_ = !held_.load();
        }
        const ChannelValues channels = brain_.step(dt);
        const bool held = held_;
        const ControllerState state = held ? safe : fromChannels(channels);
        const bool sent = sink_.send(state);

        {
            std::lock_guard lock(mutex_);
            snapshot_.state = state;
            snapshot_.measuredHz = measuredHz;
            snapshot_.sinkReady = sent;
            snapshot_.sinkMessage = sent ? sinkMessage : "Sending to the controller failed. Check the vJoy device, then stop and start again.";
        }

        std::this_thread::sleep_until(next);
        const auto now = Clock::now();
        dt = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (dt > 0.0) {
            measuredHz = measuredHz * 0.9 + (1.0 / dt) * 0.1;
        }
        next += period;
        if (next < now) {
            next = now + period;
        }
    }
}

}
