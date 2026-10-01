import ctypes
import os

import numpy as np
import torch
from torch.nn import functional as F

from pilotfly_train.layout import IMAGE_HEIGHT, IMAGE_WIDTH


def to_brain_frame(pixels: np.ndarray) -> torch.Tensor:
    blue = pixels[:, :, 0].astype(np.float32)
    green = pixels[:, :, 1].astype(np.float32)
    red = pixels[:, :, 2].astype(np.float32)
    gray = torch.from_numpy((0.299 * red + 0.587 * green + 0.114 * blue) / 255.0)
    frame = F.interpolate(gray[None, None], size=(IMAGE_HEIGHT, IMAGE_WIDTH), mode="area")
    return frame.clamp(0.0, 1.0)


def find_window_region(title_part: str) -> dict | None:
    if os.name != "nt":
        return None
    user32 = ctypes.windll.user32
    wanted = title_part.lower()
    found = []

    @ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p)
    def visit(handle, _):
        if not user32.IsWindowVisible(ctypes.c_void_p(handle)):
            return True
        buffer = ctypes.create_unicode_buffer(512)
        user32.GetWindowTextW(ctypes.c_void_p(handle), buffer, 512)
        if wanted in buffer.value.lower() and "pilotfly" not in buffer.value.lower():
            found.append(handle)
            return False
        return True

    user32.EnumWindows(visit, None)
    if not found:
        return None

    class Rect(ctypes.Structure):
        _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

    class Point(ctypes.Structure):
        _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

    rect = Rect()
    origin = Point(0, 0)
    handle = ctypes.c_void_p(found[0])
    if not user32.GetClientRect(handle, ctypes.byref(rect)):
        return None
    user32.ClientToScreen(handle, ctypes.byref(origin))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    if width <= 0 or height <= 0:
        return None
    return {"left": origin.x, "top": origin.y, "width": width, "height": height}


class GameCapture:
    def __init__(self, window_title: str = "Uncrashed", monitor: int = 1):
        import mss

        self.window_title = window_title
        self.screen = mss.mss()
        self.monitor = monitor
        self.region = None

    def open(self) -> str:
        if os.name == "nt":
            ctypes.windll.user32.SetProcessDPIAware()
        self.region = find_window_region(self.window_title)
        if self.region is None:
            if os.name == "nt":
                raise RuntimeError(
                    f"No visible window with '{self.window_title}' in its title. "
                    "Start Uncrashed in windowed or borderless mode first."
                )
            self.region = dict(self.screen.monitors[self.monitor])
            return "No game window search on this system, capturing the whole screen."
        return f"Capturing the game window, {self.region['width']} x {self.region['height']} pixels."

    def refresh(self) -> None:
        region = find_window_region(self.window_title)
        if region is not None:
            self.region = region

    def grab(self) -> torch.Tensor:
        shot = self.screen.grab(self.region)
        pixels = np.frombuffer(shot.bgra, dtype=np.uint8).reshape(shot.height, shot.width, 4)
        return to_brain_frame(pixels)
