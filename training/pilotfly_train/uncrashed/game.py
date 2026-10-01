import ctypes
import os
import time

import numpy as np
import torch

from pilotfly_train.layout import CHANNEL_COUNT, RESET_CHANNEL, STEP_SECONDS
from pilotfly_train.sim import QuadSim, SimConfig
from pilotfly_train.uncrashed.capture import GameCapture
from pilotfly_train.uncrashed.vjoy import VJoyController, safe_channels

VK_CODES = {"pause": 0x13, "scrolllock": 0x91, **{f"f{i}": 0x70 + i - 1 for i in range(1, 13)}}


def stop_key_pressed(name: str) -> bool:
    if os.name != "nt":
        return False
    code = VK_CODES.get(name.lower(), 0x13)
    return bool(ctypes.windll.user32.GetAsyncKeyState(code) & 0x8000)


class UncrashedGame:
    real_time = True

    def __init__(self, window_title: str = "Uncrashed", vjoy_device: int = 1, reset_hold: float = 1.2, settle: float = 1.5):
        self.capture = GameCapture(window_title)
        self.controller = VJoyController(vjoy_device)
        self.reset_hold = reset_hold
        self.settle = settle

    def open(self) -> str:
        message = self.capture.open()
        self.controller.open()
        self.controller.send(safe_channels())
        return message

    def close(self) -> None:
        self.controller.close()

    def frame(self) -> torch.Tensor:
        return self.capture.grab()

    def send(self, channels: np.ndarray) -> None:
        channels = np.array(channels, dtype=np.float32)
        channels[RESET_CHANNEL] = -1.0
        if not self.controller.send(channels):
            raise RuntimeError("vJoy did not accept the controller update. Check the vJoy device and start again.")

    def reset(self) -> None:
        self.capture.refresh()
        channels = safe_channels()
        self.controller.send(channels)
        time.sleep(0.3)
        channels[RESET_CHANNEL] = 1.0
        self.controller.send(channels)
        time.sleep(self.reset_hold)
        channels[RESET_CHANNEL] = -1.0
        self.controller.send(channels)
        time.sleep(self.settle)

    def wait(self, started: float) -> float:
        remaining = STEP_SECONDS - (time.perf_counter() - started)
        if remaining > 0:
            time.sleep(remaining)
        return time.perf_counter()


class SimGame:
    real_time = False

    def __init__(self, seed: int = 0):
        config = SimConfig(episode_seconds=1.0e9, randomize=False)
        self.sim = QuadSim(1, config, seed=seed)

    def open(self) -> str:
        return "Practice run in PilotFly's own simulator, not in Uncrashed."

    def close(self) -> None:
        pass

    def frame(self) -> torch.Tensor:
        return self.sim.render()

    def send(self, channels: np.ndarray) -> None:
        tensor = torch.from_numpy(np.asarray(channels, dtype=np.float32)).reshape(1, CHANNEL_COUNT)
        self.sim.step(tensor)

    def reset(self) -> None:
        self.sim.reset()

    def wait(self, started: float) -> float:
        return time.perf_counter()
