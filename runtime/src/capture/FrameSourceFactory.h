#pragma once

#include <memory>

#include "app/Config.h"
#include "capture/IFrameSource.h"

namespace pilotfly {

std::unique_ptr<IFrameSource> makeFrameSource(const Config& config);

}
