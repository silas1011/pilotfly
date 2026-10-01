import math
from dataclasses import dataclass

import torch

from pilotfly_train.layout import (
    AXIS_ARM,
    AXIS_MODE,
    AXIS_PITCH,
    AXIS_ROLL,
    AXIS_THROTTLE,
    AXIS_YAW,
    CHANNEL_COUNT,
)

GRAVITY = 9.81


@dataclass
class SimConfig:
    dt: float = 0.02
    substeps: int = 4
    image_height: int = 64
    image_width: int = 128
    source_aspect: float = 16.0 / 9.0
    episode_seconds: float = 20.0
    pillars: int = 24
    world_radius: float = 150.0
    ceiling: float = 80.0
    max_rate_roll_pitch: float = math.radians(500.0)
    max_rate_yaw: float = math.radians(300.0)
    max_angle: float = math.radians(50.0)
    angle_gain: float = 8.0
    rate_time_constant: float = 0.05
    motor_time_constant: float = 0.06
    crash_speed: float = 4.0
    crash_tilt: float = math.radians(70.0)
    randomize: bool = True


def rotation_from_rates(omega: torch.Tensor, dt: float) -> torch.Tensor:
    angle = omega.norm(dim=1, keepdim=True).clamp_min(1e-9)
    axis = omega / angle
    theta = (angle * dt)[:, :, None]
    x, y, z = axis[:, 0], axis[:, 1], axis[:, 2]
    zero = torch.zeros_like(x)
    skew = torch.stack([zero, -z, y, z, zero, -x, -y, x, zero], dim=1).reshape(-1, 3, 3)
    eye = torch.eye(3, device=omega.device).expand_as(skew)
    return eye + torch.sin(theta) * skew + (1.0 - torch.cos(theta)) * (skew @ skew)


def yaw_matrix(yaw: torch.Tensor) -> torch.Tensor:
    c, s = torch.cos(yaw), torch.sin(yaw)
    zero, one = torch.zeros_like(yaw), torch.ones_like(yaw)
    return torch.stack([c, -s, zero, s, c, zero, zero, zero, one], dim=1).reshape(-1, 3, 3)


class QuadSim:
    def __init__(self, batch: int, config: SimConfig | None = None, device: str = "cpu", seed: int = 0):
        self.batch = batch
        self.config = config or SimConfig()
        self.device = torch.device(device)
        self.generator = torch.Generator(device="cpu").manual_seed(seed)
        b, d = batch, self.device
        self.position = torch.zeros(b, 3, device=d)
        self.velocity = torch.zeros(b, 3, device=d)
        self.rotation = torch.eye(3, device=d).repeat(b, 1, 1)
        self.omega = torch.zeros(b, 3, device=d)
        self.thrust = torch.zeros(b, device=d)
        self.armed = torch.zeros(b, dtype=torch.bool, device=d)
        self.arm_blocked = torch.zeros(b, dtype=torch.bool, device=d)
        self.airborne = torch.zeros(b, dtype=torch.bool, device=d)
        self.crashed = torch.zeros(b, dtype=torch.bool, device=d)
        self.time = torch.zeros(b, device=d)
        self.armed_seconds = torch.zeros(b, device=d)
        self.start_yaw = torch.zeros(b, device=d)
        self.thrust_to_weight = torch.full((b,), 4.5, device=d)
        self.drag = torch.full((b,), 0.35, device=d)
        self.camera_tilt = torch.full((b,), math.radians(20.0), device=d)
        self.fov = torch.full((b,), math.radians(120.0), device=d)
        self.ground = torch.zeros(b, 8, device=d)
        self.sky = torch.zeros(b, 4, device=d)
        self.pillar_xy = torch.zeros(b, self.config.pillars, 2, device=d)
        self.pillar_radius = torch.ones(b, self.config.pillars, device=d)
        self.pillar_height = torch.ones(b, self.config.pillars, device=d)
        self.pillar_shade = torch.ones(b, self.config.pillars, device=d)
        self.exposure = torch.ones(b, 3, device=d)
        self.reset()

    def uniform(self, shape, low: float, high: float) -> torch.Tensor:
        if isinstance(shape, int):
            shape = (shape,)
        values = torch.rand(*shape, generator=self.generator)
        if not self.config.randomize:
            values = torch.full_like(values, 0.5)
        return (low + (high - low) * values).to(self.device)

    def reset(self, mask: torch.Tensor | None = None) -> None:
        if mask is None:
            mask = torch.ones(self.batch, dtype=torch.bool, device=self.device)
        count = int(mask.sum())
        if count == 0:
            return
        k = self.config.pillars
        yaw = self.uniform(count, -math.pi, math.pi)
        self.position[mask] = 0.0
        self.velocity[mask] = 0.0
        self.rotation[mask] = yaw_matrix(yaw)
        self.omega[mask] = 0.0
        self.thrust[mask] = 0.0
        self.armed[mask] = False
        self.arm_blocked[mask] = False
        self.airborne[mask] = False
        self.crashed[mask] = False
        self.time[mask] = 0.0
        self.armed_seconds[mask] = 0.0
        self.start_yaw[mask] = yaw
        self.thrust_to_weight[mask] = self.uniform(count, 4.0, 5.0)
        self.drag[mask] = self.uniform(count, 0.2, 0.5)
        self.camera_tilt[mask] = self.uniform(count, math.radians(10.0), math.radians(35.0))
        self.fov[mask] = self.uniform(count, math.radians(100.0), math.radians(140.0))
        ground = torch.stack([
            self.uniform(count, 0.25, 0.6),
            self.uniform(count, 0.1, 0.3),
            self.uniform(count, 0.15, 0.6),
            self.uniform(count, 0.0, 6.28),
            self.uniform(count, 0.0, 6.28),
            self.uniform(count, 0.05, 0.25),
            self.uniform(count, 1.0, 3.0),
            self.uniform(count, 0.0, 6.28),
        ], dim=1)
        self.ground[mask] = ground
        sky = torch.stack([
            self.uniform(count, 0.55, 0.95),
            self.uniform(count, -0.2, 0.2),
            self.uniform(count, -math.pi, math.pi),
            self.uniform(count, 0.2, 1.0),
        ], dim=1)
        self.sky[mask] = sky
        radius = self.uniform((count, k), 8.0, 70.0)
        angle = self.uniform((count, k), -math.pi, math.pi)
        self.pillar_xy[mask] = torch.stack([radius * torch.cos(angle), radius * torch.sin(angle)], dim=2)
        self.pillar_radius[mask] = self.uniform((count, k), 0.3, 1.5)
        self.pillar_height[mask] = self.uniform((count, k), 3.0, 15.0)
        self.pillar_shade[mask] = self.uniform((count, k), 0.05, 0.9)
        exposure = torch.stack([
            self.uniform(count, 0.8, 1.2),
            self.uniform(count, -0.08, 0.08),
            self.uniform(count, 0.0, 0.03),
        ], dim=1)
        self.exposure[mask] = exposure

    def up_axis(self) -> torch.Tensor:
        return self.rotation[:, :, 2]

    def forward_axis(self) -> torch.Tensor:
        return self.rotation[:, :, 0]

    def roll_angle(self) -> torch.Tensor:
        return torch.atan2(self.rotation[:, 2, 1], self.rotation[:, 2, 2])

    def pitch_angle(self) -> torch.Tensor:
        return torch.asin((-self.rotation[:, 2, 0]).clamp(-1.0, 1.0))

    def yaw_angle(self) -> torch.Tensor:
        return torch.atan2(self.rotation[:, 1, 0], self.rotation[:, 0, 0])

    def heading_axis(self) -> torch.Tensor:
        yaw = self.yaw_angle()
        return torch.stack([torch.cos(yaw), torch.sin(yaw), torch.zeros_like(yaw)], dim=1)

    def forward_speed(self) -> torch.Tensor:
        return (self.velocity * self.heading_axis()).sum(dim=1)

    def side_speed(self) -> torch.Tensor:
        yaw = self.yaw_angle()
        left = torch.stack([-torch.sin(yaw), torch.cos(yaw), torch.zeros_like(yaw)], dim=1)
        return (self.velocity * left).sum(dim=1)

    def step(self, channels: torch.Tensor) -> dict:
        cfg = self.config
        assert channels.shape == (self.batch, CHANNEL_COUNT)
        channels = torch.nan_to_num(channels.to(self.device)).clamp(-1.0, 1.0)
        roll_stick = channels[:, AXIS_ROLL]
        pitch_stick = channels[:, AXIS_PITCH]
        throttle = (channels[:, AXIS_THROTTLE] + 1.0) * 0.5
        yaw_stick = channels[:, AXIS_YAW]
        arm_switch = channels[:, AXIS_ARM] > 0.0
        angle_mode = channels[:, AXIS_MODE].abs() <= 0.33

        was_armed = self.armed.clone()
        can_arm = arm_switch & ~self.armed & ~self.arm_blocked & (throttle < 0.1)
        self.arm_blocked = arm_switch & ~self.armed & ~can_arm
        self.armed = (self.armed | can_arm) & arm_switch & ~self.crashed
        disarmed_in_flight = was_armed & ~self.armed & self.airborne & ~self.crashed

        dt = cfg.dt / cfg.substeps
        for _ in range(cfg.substeps):
            rate_roll = roll_stick * cfg.max_rate_roll_pitch
            rate_pitch = pitch_stick * cfg.max_rate_roll_pitch
            angle_roll = cfg.angle_gain * (roll_stick * cfg.max_angle - self.roll_angle())
            angle_pitch = cfg.angle_gain * (pitch_stick * cfg.max_angle - self.pitch_angle())
            limit = cfg.max_rate_roll_pitch
            target = torch.stack([
                torch.where(angle_mode, angle_roll.clamp(-limit, limit), rate_roll),
                torch.where(angle_mode, angle_pitch.clamp(-limit, limit), rate_pitch),
                -yaw_stick * cfg.max_rate_yaw,
            ], dim=1)
            target = torch.where(self.armed[:, None], target, torch.zeros_like(target))
            blend = dt / cfg.rate_time_constant
            self.omega = self.omega + (target - self.omega) * blend
            thrust_target = torch.where(self.armed, throttle * self.thrust_to_weight * GRAVITY, torch.zeros_like(throttle))
            self.thrust = self.thrust + (thrust_target - self.thrust) * (dt / cfg.motor_time_constant)

            on_ground = self.position[:, 2] <= 0.0
            free = ~on_ground | (self.thrust * self.up_axis()[:, 2] > GRAVITY)
            free = free & ~self.crashed
            omega = torch.where((free & self.armed)[:, None], self.omega, self.omega * 0.98)
            self.omega = torch.where(free[:, None], omega, torch.zeros_like(omega))
            self.rotation = torch.where(
                free[:, None, None], self.rotation @ rotation_from_rates(self.omega, dt), self.rotation
            )
            acceleration = self.up_axis() * self.thrust[:, None] - self.drag[:, None] * self.velocity
            acceleration[:, 2] -= GRAVITY
            self.velocity = torch.where(free[:, None], self.velocity + acceleration * dt, torch.zeros_like(self.velocity))
            self.position = self.position + self.velocity * dt

            self.airborne = self.airborne | (self.position[:, 2] > 0.3)
            touching = self.position[:, 2] <= 0.0
            speed = self.velocity.norm(dim=1)
            tilt = torch.acos(self.up_axis()[:, 2].clamp(-1.0, 1.0))
            hard = touching & self.airborne & ((speed > cfg.crash_speed) | (tilt > cfg.crash_tilt))
            self.crashed = self.crashed | hard
            landed = touching & ~hard
            self.position[:, 2] = self.position[:, 2].clamp_min(0.0)
            self.velocity = torch.where(touching[:, None], torch.zeros_like(self.velocity), self.velocity)
            level = yaw_matrix(self.yaw_angle())
            self.rotation = torch.where((landed & ~self.crashed)[:, None, None], level, self.rotation)
            self.airborne = self.airborne & ~(landed & ~self.crashed & (self.thrust < GRAVITY * 0.5))

        self.time = self.time + cfg.dt
        self.armed_seconds = torch.where(self.armed, self.armed_seconds + cfg.dt, torch.zeros_like(self.armed_seconds))
        horizontal = self.position[:, :2].norm(dim=1)
        lost = (horizontal > cfg.world_radius) | (self.position[:, 2] > cfg.ceiling)
        timeout = self.time >= cfg.episode_seconds
        done = self.crashed | lost | timeout
        return {
            "done": done,
            "crashed": self.crashed.clone(),
            "lost": lost,
            "timeout": timeout,
            "disarmed_in_flight": disarmed_in_flight,
        }

    def camera_rays(self) -> torch.Tensor:
        cfg = self.config
        h, w = cfg.image_height, cfg.image_width
        d = self.device
        tan_half = torch.tan(self.fov * 0.5)
        xs = (torch.arange(w, device=d) + 0.5) / w * 2.0 - 1.0
        ys = (torch.arange(h, device=d) + 0.5) / h * 2.0 - 1.0
        left = -xs[None, None, :] * tan_half[:, None, None]
        up = -ys[None, :, None] * (tan_half / cfg.source_aspect)[:, None, None]
        left = left.expand(self.batch, h, w)
        up = up.expand(self.batch, h, w)
        forward = torch.ones_like(left)
        c, s = torch.cos(self.camera_tilt)[:, None, None], torch.sin(self.camera_tilt)[:, None, None]
        body = torch.stack([forward * c - up * s, left, forward * s + up * c], dim=3)
        body = body / body.norm(dim=3, keepdim=True)
        world = torch.einsum("bij,bhwj->bhwi", self.rotation, body)
        return world.reshape(self.batch, h * w, 3)

    def render(self) -> torch.Tensor:
        cfg = self.config
        rays = self.camera_rays()
        origin = self.position.clone()
        origin[:, 2] = origin[:, 2] + 0.15
        dz = rays[:, :, 2]
        far = 1.0e4

        ground_t = torch.where(dz < -1e-4, -origin[:, 2:3] / dz.clamp_max(-1e-4), torch.full_like(dz, far))
        gx = origin[:, 0:1] + ground_t * rays[:, :, 0]
        gy = origin[:, 1:2] + ground_t * rays[:, :, 1]
        g = self.ground
        base, coarse_amp, coarse_freq, phase_x, phase_y = (g[:, i : i + 1] for i in range(5))
        fine_amp, fine_freq, fine_phase = g[:, 5:6], g[:, 6:7], g[:, 7:8]
        pixel = (self.fov / cfg.image_width)[:, None]
        coarse_fade = 1.0 / (1.0 + (ground_t * coarse_freq * pixel) ** 2)
        fine_fade = 1.0 / (1.0 + (ground_t * fine_freq * pixel) ** 2)
        coarse = torch.sin(gx * coarse_freq + phase_x) * torch.sin(gy * coarse_freq + phase_y)
        fine = torch.sign(torch.sin(gx * fine_freq + fine_phase) * torch.sin(gy * fine_freq - fine_phase))
        ground = base + coarse_amp * coarse * coarse_fade + fine_amp * fine * fine_fade

        sky_base, sky_slope, sun_azimuth, sun_strength = (self.sky[:, i : i + 1] for i in range(4))
        sun = torch.stack([
            torch.cos(sun_azimuth) * 0.8, torch.sin(sun_azimuth) * 0.8, torch.full_like(sun_azimuth, 0.6)
        ], dim=2)
        glow = (rays * sun).sum(dim=2).clamp_min(0.0) ** 16
        sky = sky_base + sky_slope * dz.clamp_min(0.0) + sun_strength * 0.3 * glow

        haze = sky_base
        fog = torch.exp(-ground_t / 200.0)
        image = torch.where(dz < -1e-4, ground * fog + haze * (1.0 - fog), sky)
        depth = torch.where(dz < -1e-4, ground_t, torch.full_like(ground_t, far))

        ox = origin[:, None, 0:1] - self.pillar_xy[:, :, None, 0]
        oy = origin[:, None, 1:2] - self.pillar_xy[:, :, None, 1]
        rx = rays[:, None, :, 0]
        ry = rays[:, None, :, 1]
        a = (rx * rx + ry * ry).clamp_min(1e-8)
        half_b = ox * rx + oy * ry
        c_term = ox * ox + oy * oy - (self.pillar_radius ** 2)[:, :, None]
        discriminant = half_b * half_b - a * c_term
        root = torch.sqrt(discriminant.clamp_min(0.0))
        t = (-half_b - root) / a
        hit_z = origin[:, None, 2:3] + t * rays[:, None, :, 2]
        valid = (discriminant > 0.0) & (t > 0.05) & (hit_z >= 0.0) & (hit_z <= self.pillar_height[:, :, None])
        t = torch.where(valid, t, torch.full_like(t, far))
        nearest, which = t.min(dim=1)
        shade = torch.gather(self.pillar_shade, 1, which.reshape(self.batch, -1)).reshape(nearest.shape)
        pillar_fog = torch.exp(-nearest / 200.0)
        pillar = shade * pillar_fog + haze * (1.0 - pillar_fog)
        image = torch.where(nearest < depth, pillar, image)

        gain, offset, noise = (self.exposure[:, i : i + 1] for i in range(3))
        image = (image - 0.5) * gain + 0.5 + offset
        if cfg.randomize:
            random = torch.randn(image.shape, generator=self.generator).to(self.device)
            image = image + noise * random
        image = image.clamp(0.0, 1.0)
        return image.reshape(self.batch, 1, cfg.image_height, cfg.image_width)
