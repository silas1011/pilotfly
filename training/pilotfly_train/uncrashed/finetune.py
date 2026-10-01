import argparse
import time
from pathlib import Path

import numpy as np
import torch

from pilotfly_train.brain import FlyBrain
from pilotfly_train.layout import STEP_SECONDS
from pilotfly_train.runner import OUT_DIR, load_brain, save_brain
from pilotfly_train.uncrashed.game import SimGame, UncrashedGame, stop_key_pressed
from pilotfly_train.uncrashed.vjoy import safe_channels

TUNED_PARAMETERS = ["muscle.weight", "muscle.bias", "circuit.gain", "circuit.bias"]


class Pilot:
    def __init__(self, brain: FlyBrain, device: str):
        self.brain = brain.to(device).eval()
        self.device = device
        self.dense = self.brain.circuit.dense_weights()
        self.state = self.brain.initial_state(1)

    def reset(self) -> None:
        self.state = self.brain.initial_state(1)

    @torch.no_grad()
    def step(self, frame: torch.Tensor) -> np.ndarray:
        channels, self.state = self.brain(frame.to(self.device), self.state, self.dense)
        return channels[0].cpu().numpy()


class Stopped(Exception):
    pass


def run_episode(game, pilot: Pilot, args, endless: bool = False) -> dict:
    game.reset()
    pilot.reset()
    previous = game.frame()
    moving_seconds = 0.0
    still_seconds = 0.0
    slow_steps = 0
    steps = int(args.episode_seconds / STEP_SECONDS)
    reason = "time is up"
    clock = time.perf_counter()
    step = -1
    while endless or step + 1 < steps:
        step += 1
        if game.real_time and stop_key_pressed(args.stop_key):
            game.send(safe_channels())
            raise Stopped()
        frame = game.frame()
        game.send(pilot.step(frame))
        change = float((frame - previous).abs().mean())
        previous = frame
        if change > args.motion_threshold:
            moving_seconds += STEP_SECONDS
            still_seconds = 0.0
        else:
            still_seconds += STEP_SECONDS
        took_off = moving_seconds >= args.takeoff_seconds
        if took_off and still_seconds >= args.crash_seconds:
            reason = "picture stopped moving (crash or landing)"
            break
        if not took_off and (step + 1) * STEP_SECONDS >= args.idle_seconds:
            reason = "never started moving"
            break
        before = clock
        clock = game.wait(clock)
        if game.real_time and clock - before > STEP_SECONDS * 1.5:
            slow_steps += 1
    game.send(safe_channels())
    return {"score": moving_seconds, "reason": reason, "slow_steps": slow_steps}


def tuned_tensors(brain: FlyBrain) -> list[torch.Tensor]:
    named = dict(brain.named_parameters())
    return [named[name] for name in TUNED_PARAMETERS]


def read_vector(brain: FlyBrain) -> torch.Tensor:
    return torch.cat([tensor.detach().reshape(-1).cpu() for tensor in tuned_tensors(brain)])


def write_vector(brain: FlyBrain, vector: torch.Tensor) -> None:
    offset = 0
    with torch.no_grad():
        for tensor in tuned_tensors(brain):
            count = tensor.numel()
            tensor.copy_(vector[offset : offset + count].reshape(tensor.shape).to(tensor.device))
            offset += count


def noise_scale(brain: FlyBrain) -> torch.Tensor:
    scales = []
    for tensor in tuned_tensors(brain):
        scale = max(float(tensor.detach().abs().mean()), 0.01)
        scales.append(torch.full((tensor.numel(),), scale))
    return torch.cat(scales)


def rank_weights(scores: np.ndarray) -> np.ndarray:
    scores = np.asarray(scores, dtype=np.float64)
    if len(scores) < 2 or float(scores.max()) == float(scores.min()):
        return np.zeros(len(scores), dtype=np.float32)
    lower = (scores[None, :] < scores[:, None]).sum(axis=1)
    equal = (scores[None, :] == scores[:, None]).sum(axis=1)
    rank = lower + (equal - 1) / 2.0
    return (rank / (len(scores) - 1) - 0.5).astype(np.float32)


def train(game, brain: FlyBrain, args, device: str) -> None:
    pilot = Pilot(brain, device)
    centre = read_vector(brain)
    scale = noise_scale(brain)
    generator = torch.Generator().manual_seed(args.seed)
    out = Path(args.out)
    pairs = max(1, args.population // 2)
    print(f"Fine-tuning {centre.numel()} values. {args.generations} generations, {pairs * 2} flights each.")
    try:
        for generation in range(1, args.generations + 1):
            noises, scores = [], []
            for pair in range(pairs):
                noise = torch.randn(centre.shape, generator=generator) * scale
                for sign in (1.0, -1.0):
                    write_vector(brain, centre + sign * args.sigma * noise)
                    result = run_episode(game, pilot, args)
                    noises.append(sign * noise)
                    scores.append(result["score"])
                    slow = f", {result['slow_steps']} slow steps" if result["slow_steps"] else ""
                    print(
                        f"  generation {generation} flight {len(scores)}/{pairs * 2}: "
                        f"{result['score']:.1f} s moving, {result['reason']}{slow}",
                        flush=True,
                    )
            scores = np.array(scores, dtype=np.float32)
            weights = rank_weights(scores)
            update = sum(float(weight) * noise for weight, noise in zip(weights, noises)) / len(noises)
            centre = centre + args.learning_rate * update
            write_vector(brain, centre)
            save_brain(brain, out, {"stage": "C", "generation": generation, "mean_score": float(scores.mean())})
            print(
                f"generation {generation}: mean {scores.mean():.1f} s, best {scores.max():.1f} s, saved {out}",
                flush=True,
            )
    except Stopped:
        write_vector(brain, centre)
        save_brain(brain, out, {"stage": "C", "stopped": True})
        print(f"Stopped with the stop key. Progress saved to {out}")


def fly(game, brain: FlyBrain, args, device: str) -> None:
    pilot = Pilot(brain, device)
    print(f"Flying. Press {args.stop_key} to stop.")
    try:
        while True:
            result = run_episode(game, pilot, args, endless=game.real_time)
            print(f"flight ended after {result['score']:.1f} s moving: {result['reason']}", flush=True)
            if not game.real_time:
                break
    except Stopped:
        print("Stopped with the stop key.")


def calibrate(args) -> None:
    from pilotfly_train.uncrashed.capture import GameCapture

    capture = GameCapture(args.window)
    print(capture.open())
    print("Fly by hand, crash, and sit still. Watch the number to choose --motion-threshold.")
    previous = capture.grab()
    end = time.time() + args.seconds
    while time.time() < end:
        started = time.perf_counter()
        values = []
        for _ in range(25):
            tick = time.perf_counter()
            frame = capture.grab()
            values.append(float((frame - previous).abs().mean()))
            previous = frame
            time.sleep(max(0.0, STEP_SECONDS - (time.perf_counter() - tick)))
        rate = 25 / (time.perf_counter() - started)
        print(f"picture change {np.mean(values):.4f} (largest {np.max(values):.4f}), {rate:.0f} pictures per second", flush=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["train", "fly", "calibrate"])
    parser.add_argument("--checkpoint", default=str(OUT_DIR / "brain_stage_b.pt"))
    parser.add_argument("--out", default=str(OUT_DIR / "brain_stage_c.pt"))
    parser.add_argument("--window", default="Uncrashed")
    parser.add_argument("--vjoy-device", type=int, default=1)
    parser.add_argument("--stop-key", default="Pause")
    parser.add_argument("--practice", action="store_true")
    parser.add_argument("--device", default="auto")
    parser.add_argument("--generations", type=int, default=40)
    parser.add_argument("--population", type=int, default=8)
    parser.add_argument("--sigma", type=float, default=0.05)
    parser.add_argument("--learning-rate", type=float, default=0.3)
    parser.add_argument("--episode-seconds", type=float, default=20.0)
    parser.add_argument("--motion-threshold", type=float, default=0.004)
    parser.add_argument("--takeoff-seconds", type=float, default=0.5)
    parser.add_argument("--crash-seconds", type=float, default=1.5)
    parser.add_argument("--idle-seconds", type=float, default=6.0)
    parser.add_argument("--reset-hold", type=float, default=1.2)
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--seed", type=int, default=0)
    args = parser.parse_args()

    if args.mode == "calibrate":
        calibrate(args)
        return

    device = args.device
    if device == "auto":
        device = "cuda" if torch.cuda.is_available() else "cpu"
    brain = load_brain(Path(args.checkpoint), device)
    if args.practice:
        game = SimGame(args.seed)
    else:
        game = UncrashedGame(args.window, args.vjoy_device, args.reset_hold)
    print(game.open())
    try:
        if args.mode == "train":
            train(game, brain, args, device)
        else:
            fly(game, brain, args, device)
    finally:
        game.close()


if __name__ == "__main__":
    main()
