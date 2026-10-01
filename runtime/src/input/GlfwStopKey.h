#pragma once

#include "app/Config.h"
#include "input/IStopKey.h"

struct GLFWwindow;

namespace pilotfly {

class GlfwStopKey : public IStopKey {
public:
    GlfwStopKey(GLFWwindow* window, const StopKey& key);

    std::string keyName() const override;
    bool consumePress() override;
    bool usableFromAnyThread() const override;

private:
    GLFWwindow* window_;
    StopKey key_;
    int glfwKey_;
    bool wasDown_ = false;
};

}
