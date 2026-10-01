import ctypes
import os
from pathlib import Path

import numpy as np

from pilotfly_train.layout import AXIS_COUNT, BUTTON_COUNT, CHANNEL_COUNT

EDGETX_AXIS_MAX = 2047
HID_USAGES = [0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37]
STATUS_OWN = 0
STATUS_FREE = 1
STATUS_BUSY = 2
STATUS_MISSING = 3
HAT_NEUTRAL = 0xFFFFFFFF
DEFAULT_DLL = r"C:\Program Files\vJoy\x64\vJoyInterface.dll"


class JoystickPosition(ctypes.Structure):
    _fields_ = [
        ("bDevice", ctypes.c_ubyte),
        ("wThrottle", ctypes.c_long),
        ("wRudder", ctypes.c_long),
        ("wAileron", ctypes.c_long),
        ("wAxisX", ctypes.c_long),
        ("wAxisY", ctypes.c_long),
        ("wAxisZ", ctypes.c_long),
        ("wAxisXRot", ctypes.c_long),
        ("wAxisYRot", ctypes.c_long),
        ("wAxisZRot", ctypes.c_long),
        ("wSlider", ctypes.c_long),
        ("wDial", ctypes.c_long),
        ("wWheel", ctypes.c_long),
        ("wAccelerator", ctypes.c_long),
        ("wBrake", ctypes.c_long),
        ("wClutch", ctypes.c_long),
        ("wSteering", ctypes.c_long),
        ("wAxisVX", ctypes.c_long),
        ("wAxisVY", ctypes.c_long),
        ("lButtons", ctypes.c_long),
        ("bHats", ctypes.c_ulong),
        ("bHatsEx1", ctypes.c_ulong),
        ("bHatsEx2", ctypes.c_ulong),
        ("bHatsEx3", ctypes.c_ulong),
        ("lButtonsEx1", ctypes.c_long),
        ("lButtonsEx2", ctypes.c_long),
        ("lButtonsEx3", ctypes.c_long),
        ("wAxisVZ", ctypes.c_long),
        ("wAxisVBRX", ctypes.c_long),
        ("wAxisVBRY", ctypes.c_long),
        ("wAxisVBRZ", ctypes.c_long),
    ]


AXIS_FIELDS = ["wAxisX", "wAxisY", "wAxisZ", "wAxisXRot", "wAxisYRot", "wAxisZRot", "wSlider", "wDial"]


def axis_to_range(value: float, low: int, high: int) -> int:
    if not np.isfinite(value):
        value = 0.0
    value = min(1.0, max(-1.0, float(value)))
    step = int(round((value + 1.0) * 0.5 * EDGETX_AXIS_MAX))
    return low + int(round(step / EDGETX_AXIS_MAX * (high - low)))


def button_mask(channels) -> int:
    mask = 0
    for button in range(BUTTON_COUNT):
        if float(channels[AXIS_COUNT + button]) > 0.0:
            mask |= 1 << button
    return mask


def safe_channels() -> np.ndarray:
    channels = np.full(CHANNEL_COUNT, -1.0, dtype=np.float32)
    channels[:AXIS_COUNT] = 0.0
    channels[2] = -1.0
    channels[4] = -1.0
    return channels


class VJoyController:
    def __init__(self, device: int = 1, dll_path: str | None = None):
        self.device = device
        self.dll_path = dll_path or os.environ.get("VJOY_DLL", DEFAULT_DLL)
        self.dll = None
        self.ranges = [(1, 32768)] * AXIS_COUNT

    def open(self) -> None:
        if os.name != "nt":
            raise RuntimeError("vJoy only exists on Windows.")
        if not Path(self.dll_path).exists():
            raise RuntimeError(
                f"vJoyInterface.dll not found at {self.dll_path}. Install vJoy 2.2.2.0 or set the VJOY_DLL variable."
            )
        dll = ctypes.CDLL(self.dll_path)
        dll.GetVJDStatus.restype = ctypes.c_int
        dll.GetVJDButtonNumber.restype = ctypes.c_int
        if not dll.vJoyEnabled():
            raise RuntimeError("The vJoy driver is not enabled. Install vJoy and restart.")
        status = dll.GetVJDStatus(ctypes.c_uint(self.device))
        if status == STATUS_BUSY:
            raise RuntimeError(f"vJoy device {self.device} is used by another program. Close PilotFly or other feeders.")
        if status == STATUS_MISSING:
            raise RuntimeError(f"vJoy device {self.device} does not exist. Enable it in 'Configure vJoy'.")
        if status not in (STATUS_OWN, STATUS_FREE):
            raise RuntimeError(f"vJoy device {self.device} is in an unknown state. Restart Windows and try again.")
        if status == STATUS_FREE and not dll.AcquireVJD(ctypes.c_uint(self.device)):
            raise RuntimeError(f"Could not take control of vJoy device {self.device}.")
        self.dll = dll
        missing_axis = any(not dll.GetVJDAxisExist(ctypes.c_uint(self.device), ctypes.c_uint(u)) for u in HID_USAGES)
        if missing_axis or dll.GetVJDButtonNumber(ctypes.c_uint(self.device)) < BUTTON_COUNT:
            self.close()
            raise RuntimeError(
                f"vJoy device {self.device} needs the axes X, Y, Z, Rx, Ry, Rz, Slider, Dial/Slider2 and 24 buttons."
            )
        ranges = []
        for usage in HID_USAGES:
            low, high = ctypes.c_long(0), ctypes.c_long(0)
            dll.GetVJDAxisMin(ctypes.c_uint(self.device), ctypes.c_uint(usage), ctypes.byref(low))
            dll.GetVJDAxisMax(ctypes.c_uint(self.device), ctypes.c_uint(usage), ctypes.byref(high))
            ranges.append((low.value, high.value) if high.value > low.value else (1, 32768))
        self.ranges = ranges

    def send(self, channels) -> bool:
        if self.dll is None:
            return False
        report = JoystickPosition()
        report.bDevice = self.device
        for axis, field in enumerate(AXIS_FIELDS):
            low, high = self.ranges[axis]
            setattr(report, field, axis_to_range(float(channels[axis]), low, high))
        report.lButtons = button_mask(channels)
        report.bHats = HAT_NEUTRAL
        report.bHatsEx1 = HAT_NEUTRAL
        report.bHatsEx2 = HAT_NEUTRAL
        report.bHatsEx3 = HAT_NEUTRAL
        return bool(self.dll.UpdateVJD(ctypes.c_uint(self.device), ctypes.byref(report)))

    def close(self) -> None:
        if self.dll is not None:
            self.send(safe_channels())
            self.dll.RelinquishVJD(ctypes.c_uint(self.device))
            self.dll = None
