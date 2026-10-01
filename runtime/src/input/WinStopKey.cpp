#include "input/WinStopKey.h"

#include <windows.h>

namespace pilotfly {

namespace {

int toVirtualKey(const StopKey& key) {
    switch (key.kind) {
        case StopKeyKind::Pause:
            return VK_PAUSE;
        case StopKeyKind::ScrollLock:
            return VK_SCROLL;
        case StopKeyKind::Function:
            if (key.functionNumber >= 1 && key.functionNumber <= 12) {
                return VK_F1 + key.functionNumber - 1;
            }
            return VK_PAUSE;
    }
    return VK_PAUSE;
}

}

WinStopKey::WinStopKey(const StopKey& key) : key_(key), virtualKey_(toVirtualKey(key)) {}

std::string WinStopKey::keyName() const {
    return stopKeyName(key_);
}

bool WinStopKey::usableFromAnyThread() const {
    return true;
}

bool WinStopKey::consumePress() {
    const bool down = (GetAsyncKeyState(virtualKey_) & 0x8000) != 0;
    const bool pressed = down && !wasDown_;
    wasDown_ = down;
    return pressed;
}

}
