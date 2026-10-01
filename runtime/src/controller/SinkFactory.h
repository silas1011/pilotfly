#pragma once

#include <memory>

#include "app/Config.h"
#include "controller/IControllerSink.h"

namespace pilotfly {

std::unique_ptr<IControllerSink> makeControllerSink(const Config& config);

}
