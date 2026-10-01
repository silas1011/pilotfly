#include "input/StopKeyFactory.h"

#ifdef _WIN32
#include "input/WinStopKey.h"
#else
#include "input/GlfwStopKey.h"
#endif

namespace pilotfly {

std::unique_ptr<IStopKey> makeStopKey(const StopKey& key, GLFWwindow* window) {
#ifdef _WIN32
    (void)window;
    return std::make_unique<WinStopKey>(key);
#else
    return std::make_unique<GlfwStopKey>(window, key);
#endif
}

}
