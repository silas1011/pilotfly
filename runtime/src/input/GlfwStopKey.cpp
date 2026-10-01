#include "input/GlfwStopKey.h"

#include <GLFW/glfw3.h>

namespace pilotfly {

namespace {

int toGlfwKey(const StopKey& key) {
    switch (key.kind) {
        case StopKeyKind::Pause:
            return GLFW_KEY_PAUSE;
        case StopKeyKind::ScrollLock:
            return GLFW_KEY_SCROLL_LOCK;
        case StopKeyKind::Function:
            return GLFW_KEY_F1 + key.functionNumber - 1;
    }
    return GLFW_KEY_PAUSE;
}

}

GlfwStopKey::GlfwStopKey(GLFWwindow* window, const StopKey& key)
    : window_(window), key_(key), glfwKey_(toGlfwKey(key)) {
    glfwSetInputMode(window_, GLFW_STICKY_KEYS, GLFW_TRUE);
}

std::string GlfwStopKey::keyName() const {
    return stopKeyName(key_);
}

bool GlfwStopKey::usableFromAnyThread() const {
    return false;
}

bool GlfwStopKey::consumePress() {
    const bool down = glfwGetKey(window_, glfwKey_) == GLFW_PRESS;
    const bool pressed = down && !wasDown_;
    wasDown_ = down;
    return pressed;
}

}
