import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import torch

from pilotfly_train.brain import FlyBrain
from pilotfly_train.sim import QuadSim, SimConfig

OUT_DIR = Path(__file__).resolve().parent.parent / "out"


def pick_device(name: str = "auto") -> str:
    if name != "auto":
        return name
    if torch.cuda.is_available():
        return "cuda"
    return "cpu"


def trainable_state(brain: FlyBrain) -> dict:
    names = {name for name, parameter in brain.named_parameters() if parameter.requires_grad}
    return {name: value.detach().cpu() for name, value in brain.state_dict().items() if name in names}


def save_brain(brain: FlyBrain, path: Path | str, extra: dict | None = None) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save({"trainable": trainable_state(brain), "extra": extra or {}}, path)


def load_brain(path: Path | str | None = None, device: str = "cpu") -> FlyBrain:
    brain = FlyBrain()
    if path:
        checkpoint = torch.load(path, map_location="cpu", weights_only=True)
        brain.load_state_dict(checkpoint["trainable"], strict=False)
    return brain.to(device)


class Runner:
    def __init__(self, brain: FlyBrain, batch: int, seed: int = 0, config: SimConfig | None = None, workers: int | None = None):
        self.brain = brain
        self.batch = batch
        self.device = brain.sampler.device
        self.sim = QuadSim(batch, config, device=str(self.device), seed=seed)
        self.eye_state = brain.initial_eye_state(batch)
        self.v = brain.initial_circuit_state(batch)
        self.use_sparse = self.device.type != "cuda"
        self.workers = workers or max(1, min(10, (os.cpu_count() or 4) - 2))
        self.pool = ThreadPoolExecutor(self.workers) if self.use_sparse else None

    @torch.no_grad()
    def observe(self) -> torch.Tensor:
        brain = self.brain
        frame = self.sim.render()
        size = brain.eye.state_size
        left_hex, right_hex = brain.hexals(frame)
        hexals = torch.cat([left_hex, right_hex], dim=0)
        state = torch.cat([self.eye_state[:, :size], self.eye_state[:, size:]], dim=0)
        if self.use_sparse:
            state = brain.eye.step_sparse_parallel(hexals, state, self.pool, self.workers)
        else:
            state = brain.eye.step(hexals, state)
        motion = torch.relu(brain.eye.motion(state) - brain.motion_rest)
        self.eye_state = torch.cat([state[: self.batch], state[self.batch :]], dim=1)
        return torch.stack([motion[: self.batch], motion[self.batch :]], dim=1)

    @torch.no_grad()
    def reset_done(self, done: torch.Tensor) -> None:
        if not bool(done.any()):
            return
        self.sim.reset(done)
        self.eye_state[done] = self.brain.initial_eye_state(1)
        self.v[done] = 0.0

    @torch.no_grad()
    def reset_all(self) -> None:
        self.reset_done(torch.ones(self.batch, dtype=torch.bool, device=self.device))


@torch.no_grad()
def evaluate(brain: FlyBrain, batch: int = 64, seed: int = 1234, controller=None) -> dict:
    runner = Runner(brain, batch, seed=seed)
    sim = runner.sim
    steps = int(sim.config.episode_seconds / sim.config.dt)
    alive = torch.ones(batch, dtype=torch.bool, device=runner.device)
    flight_time = torch.zeros(batch, device=runner.device)
    survived = torch.zeros(batch, device=runner.device)
    height_sum = torch.zeros(batch, device=runner.device)
    armed_ever = torch.zeros(batch, dtype=torch.bool, device=runner.device)
    crashed = torch.zeros(batch, dtype=torch.bool, device=runner.device)
    lost = torch.zeros(batch, dtype=torch.bool, device=runner.device)
    distance = torch.zeros(batch, device=runner.device)
    for _ in range(steps):
        motion = runner.observe()
        raw, runner.v = brain.act(motion, runner.v)
        channels = torch.tanh(raw) if controller is None else controller(sim)
        info = sim.step(channels)
        flying = alive & sim.airborne & ~sim.crashed
        flight_time += flying.float() * sim.config.dt
        height_sum += flying.float() * sim.position[:, 2] * sim.config.dt
        survived += alive.float() * sim.config.dt
        armed_ever |= sim.armed
        crashed |= alive & info["crashed"]
        lost |= alive & info["lost"]
        heading = torch.stack([torch.cos(sim.start_yaw), torch.sin(sim.start_yaw)], dim=1)
        progress = (sim.position[:, :2] * heading).sum(dim=1)
        distance = torch.where(alive, progress, distance)
        alive &= ~info["done"]
    return {
        "flight_seconds": float(flight_time.mean()),
        "armed": float(armed_ever.float().mean()),
        "crashed": float(crashed.float().mean()),
        "lost": float(lost.float().mean()),
        "mean_height": float((height_sum / flight_time.clamp_min(1e-6)).mean()),
        "forward_metres": float(distance.mean()),
        "episode_seconds": float(sim.config.episode_seconds),
    }


def format_metrics(metrics: dict) -> str:
    return (
        f"airborne {metrics['flight_seconds']:.1f}/{metrics['episode_seconds']:.0f} s, "
        f"armed {metrics['armed'] * 100:.0f}%, crashed {metrics['crashed'] * 100:.0f}%, "
        f"flew away {metrics['lost'] * 100:.0f}%, height {metrics['mean_height']:.1f} m, "
        f"forward {metrics['forward_metres']:.1f} m"
    )
