#pragma once

#include "core/ControllerState.h"
#include "core/Tx12Layout.h"

namespace pilotfly {

ControllerState safeState(const Tx12Layout& layout);

}
