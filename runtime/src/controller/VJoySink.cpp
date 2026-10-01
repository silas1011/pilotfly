#include "controller/VJoySink.h"

#include <windows.h>
#include <winioctl.h>

#include "vjoyinterface.h"

namespace pilotfly {

namespace {

constexpr int kMaxDeviceId = 16;
constexpr DWORD kHatNeutral = 0xFFFFFFFF;

constexpr std::array<UINT, kAxisCount> kAxisUsages = {
    HID_USAGE_X,
    HID_USAGE_Y,
    HID_USAGE_Z,
    HID_USAGE_RX,
    HID_USAGE_RY,
    HID_USAGE_RZ,
    HID_USAGE_SL0,
    HID_USAGE_SL1};

std::string layoutMessage(int deviceId) {
    return "vJoy device " + std::to_string(deviceId) +
           " needs 8 axes (X, Y, Z, Rx, Ry, Rz, Slider, Dial/Slider2) and 24 buttons. "
           "Open 'Configure vJoy' and enable them.";
}

}

VJoySink::VJoySink(int deviceId) : deviceId_(deviceId) {}

VJoySink::~VJoySink() {
    close();
}

std::string VJoySink::name() const {
    return "vJoy device " + std::to_string(deviceId_);
}

SinkStatus VJoySink::open() {
    std::lock_guard lock(mutex_);
    if (open_) {
        return {true, name() + " is active"};
    }
    if (deviceId_ < 1 || deviceId_ > kMaxDeviceId) {
        return fail("vJoy device number must be between 1 and 16. Fix vjoy_device in pilotfly.ini.");
    }
    if (!vJoyEnabled()) {
        return fail("vJoy driver not found. Install vJoy and restart PilotFly.");
    }

    WORD dllVersion = 0;
    WORD driverVersion = 0;
    if (!DriverMatch(&dllVersion, &driverVersion)) {
        return fail("The installed vJoy driver does not match PilotFly. Install vJoy 2.2.2.0 and restart PilotFly.");
    }

    const UINT id = static_cast<UINT>(deviceId_);
    const VjdStat status = GetVJDStatus(id);
    if (status == VJD_STAT_BUSY) {
        return fail(name() + " is used by another program. Close that program and restart PilotFly.");
    }
    if (status == VJD_STAT_MISS) {
        return fail(name() + " does not exist. Open 'Configure vJoy' and enable device " +
                    std::to_string(deviceId_) + ".");
    }
    if (status != VJD_STAT_FREE && status != VJD_STAT_OWN) {
        return fail(name() + " is in an unknown state. Restart Windows and try again.");
    }

    if (status == VJD_STAT_FREE && !AcquireVJD(id)) {
        return fail("Could not take control of " + name() +
                    ". Close other programs that use vJoy and restart PilotFly.");
    }
    acquired_ = true;

    for (int i = 0; i < kAxisCount; ++i) {
        if (!GetVJDAxisExist(id, kAxisUsages[i])) {
            return fail(layoutMessage(deviceId_));
        }
    }
    if (GetVJDButtonNumber(id) < kButtonCount) {
        return fail(layoutMessage(deviceId_));
    }

    for (int i = 0; i < kAxisCount; ++i) {
        LONG minimum = 0;
        LONG maximum = 0;
        if (!GetVJDAxisMin(id, kAxisUsages[i], &minimum) ||
            !GetVJDAxisMax(id, kAxisUsages[i], &maximum) ||
            maximum <= minimum) {
            return fail("Could not read the axis range of " + name() +
                        ". Reinstall vJoy and restart PilotFly.");
        }
        axisMin_[i] = minimum;
        axisMax_[i] = maximum;
    }

    open_ = true;
    return {true, name() + " is active"};
}

void VJoySink::close() {
    std::lock_guard lock(mutex_);
    release();
}

bool VJoySink::send(const ControllerState& state) {
    std::lock_guard lock(mutex_);
    if (!open_) {
        return false;
    }

    std::array<LONG, kAxisCount> values{};
    for (int i = 0; i < kAxisCount; ++i) {
        values[i] = static_cast<LONG>(axisToRange(state.axes[i], axisMin_[i], axisMax_[i]));
    }

    DWORD buttons = 0;
    for (int i = 0; i < kButtonCount; ++i) {
        if (state.buttons[i]) {
            buttons |= static_cast<DWORD>(1) << i;
        }
    }

    JOYSTICK_POSITION report{};
    report.bDevice = static_cast<BYTE>(deviceId_);
    report.wAxisX = values[0];
    report.wAxisY = values[1];
    report.wAxisZ = values[2];
    report.wAxisXRot = values[3];
    report.wAxisYRot = values[4];
    report.wAxisZRot = values[5];
    report.wSlider = values[6];
    report.wDial = values[7];
    report.lButtons = static_cast<LONG>(buttons);
    report.bHats = kHatNeutral;
    report.bHatsEx1 = kHatNeutral;
    report.bHatsEx2 = kHatNeutral;
    report.bHatsEx3 = kHatNeutral;

    return UpdateVJD(static_cast<UINT>(deviceId_), &report) != FALSE;
}

SinkStatus VJoySink::fail(const std::string& message) {
    release();
    return {false, message};
}

void VJoySink::release() {
    if (acquired_) {
        RelinquishVJD(static_cast<UINT>(deviceId_));
    }
    acquired_ = false;
    open_ = false;
}

}
