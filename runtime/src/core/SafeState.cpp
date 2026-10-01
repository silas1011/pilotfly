#include "core/SafeState.h"

namespace pilotfly {

ControllerState safeState(const Tx12Layout& layout) {
    ControllerState state;
    state.axes[layout.throttleAxis] = -1.0f;
    for (const auto& spec : layout.switches) {
        if (spec.role == SwitchRole::Arm && spec.axis >= 0) {
            state.axes[spec.axis] = -1.0f;
        }
    }
    return state;
}

}
