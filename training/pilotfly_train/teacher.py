import math
from dataclasses import dataclass

import torch

from pilotfly_train.layout import (
    AXIS_ARM,
    AXIS_COUNT,
    AXIS_MODE,
    AXIS_PITCH,
    AXIS_ROLL,
    AXIS_THROTTLE,
    AXIS_YAW,
    CHANNEL_COUNT,
)
from pilotfly_train.sim import QuadSim


@dataclass
class TeacherConfig:
    arm_after_seconds: float = 0.4
    spool_seconds: float = 0.6
    target_height: float = 3.0
    target_speed: float = 3.0
    cruise_after_height: float = 1.5
    height_gain: float = 0.35
    climb_gain: float = 0.25
    speed_gain: float = 0.25
    side_gain: float = 0.3
    yaw_gain: float = 1.5
    max_tilt_command: float = 0.6


def wrap_angle(angle: torch.Tensor) -> torch.Tensor:
    return torch.atan2(torch.sin(angle), torch.cos(angle))


def teacher_channels(sim: QuadSim, config: TeacherConfig | None = None) -> torch.Tensor:
    cfg = config or TeacherConfig()
    channels = torch.full((sim.batch, CHANNEL_COUNT), -1.0, device=sim.device)
    channels[:, :AXIS_COUNT] = 0.0

    ready = sim.time >= cfg.arm_after_seconds
    flying = ready & sim.armed & (sim.armed_seconds >= cfg.spool_seconds)
    height = sim.position[:, 2]
    climb = sim.velocity[:, 2]

    hover = 1.0 / sim.thrust_to_weight
    throttle = hover + cfg.height_gain * (cfg.target_height - height) / sim.thrust_to_weight * 2.0
    throttle = throttle - cfg.climb_gain * climb / sim.thrust_to_weight * 2.0
    tilt = sim.up_axis()[:, 2].clamp_min(0.5)
    throttle = (throttle / tilt).clamp(0.0, 1.0)
    throttle_stick = torch.where(flying, throttle * 2.0 - 1.0, torch.full_like(throttle, -1.0))

    cruising = flying & (height > cfg.cruise_after_height)
    pitch = (cfg.speed_gain * (cfg.target_speed - sim.forward_speed())).clamp(-cfg.max_tilt_command, cfg.max_tilt_command)
    roll = (cfg.side_gain * sim.side_speed()).clamp(-cfg.max_tilt_command, cfg.max_tilt_command)
    yaw_error = wrap_angle(sim.yaw_angle() - sim.start_yaw)
    yaw = (cfg.yaw_gain * yaw_error / math.pi).clamp(-0.5, 0.5)
    zero = torch.zeros_like(pitch)

    channels[:, AXIS_ROLL] = torch.where(cruising, roll, zero)
    channels[:, AXIS_PITCH] = torch.where(cruising, pitch, zero)
    channels[:, AXIS_THROTTLE] = throttle_stick
    channels[:, AXIS_YAW] = torch.where(flying, yaw, zero)
    switch_on = ready & ~sim.arm_blocked
    channels[:, AXIS_ARM] = torch.where(switch_on, torch.ones_like(pitch), -torch.ones_like(pitch))
    channels[:, AXIS_MODE] = 0.0
    return channels
