#include "controller/DummySink.h"

namespace pilotfly {

std::string DummySink::name() const {
    return "Dummy controller";
}

SinkStatus DummySink::open() {
    std::lock_guard lock(mutex_);
    open_ = true;
    return {true, "Dummy controller active, nothing is sent to Windows"};
}

void DummySink::close() {
    std::lock_guard lock(mutex_);
    open_ = false;
}

bool DummySink::send(const ControllerState& state) {
    std::lock_guard lock(mutex_);
    if (!open_) {
        return false;
    }
    last_ = state;
    ++sendCount_;
    return true;
}

bool DummySink::isOpen() const {
    std::lock_guard lock(mutex_);
    return open_;
}

int DummySink::sendCount() const {
    std::lock_guard lock(mutex_);
    return sendCount_;
}

ControllerState DummySink::lastState() const {
    std::lock_guard lock(mutex_);
    return last_;
}

}
