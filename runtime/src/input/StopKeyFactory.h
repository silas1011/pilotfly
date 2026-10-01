#pragma once

#include <memory>

#include "app/Config.h"
#include "input/IStopKey.h"

struct GLFWwindow;

namespace pilotfly {

std::unique_ptr<IStopKey> makeStopKey(const StopKey& key, GLFWwindow* window);

}
