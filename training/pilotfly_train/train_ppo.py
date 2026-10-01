import argparse
import math
import time

import torch
from torch import nn

from pilotfly_train.layout import CHANNEL_COUNT
from pilotfly_train.runner import OUT_DIR, Runner, evaluate, format_metrics, load_brain, pick_device, save_brain
from pilotfly_train.sim import QuadSim
from pilotfly_train.teacher import teacher_channels
from pilotfly_train.train_imitation import channel_weights

REWARD_SCALE = 0.1


class Critic(nn.Module):
    def __init__(self):
        super().__init__()
        self.net = nn.Sequential(nn.Linear(14, 64), nn.Tanh(), nn.Linear(64, 64), nn.Tanh(), nn.Linear(64, 1))

    def forward(self, features: torch.Tensor) -> torch.Tensor:
        return self.net(features).squeeze(-1)


def critic_features(sim: QuadSim) -> torch.Tensor:
    return torch.stack([
        sim.position[:, 2] / 5.0,
        sim.forward_speed() / 5.0,
        sim.side_speed() / 5.0,
        sim.velocity[:, 2] / 5.0,
        sim.up_axis()[:, 2],
        sim.roll_angle(),
        sim.pitch_angle(),
        sim.omega[:, 0] / 5.0,
        sim.omega[:, 1] / 5.0,
        sim.omega[:, 2] / 5.0,
        sim.armed.float(),
        sim.airborne.float(),
        sim.time / sim.config.episode_seconds,
        sim.thrust_to_weight / 5.0,
    ], dim=1)


def reward(sim: QuadSim, info: dict, action: torch.Tensor, previous: torch.Tensor) -> torch.Tensor:
    flying = (sim.airborne & sim.armed & ~sim.crashed).float()
    height = sim.position[:, 2]
    value = flying
    value = value + flying * torch.exp(-(((height - 3.0) / 2.0) ** 2))
    value = value + 0.5 * flying * sim.forward_speed().clamp(0.0, 3.0) / 3.0
    value = value + 0.3 * flying * sim.up_axis()[:, 2]
    value = value - 0.05 * (action - previous).abs().mean(dim=1)
    value = value - 10.0 * info["crashed"].float()
    value = value - 5.0 * info["lost"].float()
    value = value - 5.0 * info["disarmed_in_flight"].float()
    return value * REWARD_SCALE


def log_probability(mean: torch.Tensor, log_std: torch.Tensor, sample: torch.Tensor) -> torch.Tensor:
    variance = torch.exp(2.0 * log_std)
    return (-((sample - mean) ** 2) / (2.0 * variance) - log_std - 0.5 * math.log(2.0 * math.pi)).sum(dim=1)


def collect(runner: Runner, critic: Critic, log_std: torch.Tensor, steps: int, previous: torch.Tensor) -> dict:
    brain, sim = runner.brain, runner.sim
    keys = ["motion", "sample", "log_prob", "value", "reward", "done", "reset", "teacher", "features"]
    data = {key: [] for key in keys}
    start_v = runner.v.clone()
    reset_before = torch.zeros(runner.batch, dtype=torch.bool, device=runner.device)
    for _ in range(steps):
        motion = runner.observe()
        with torch.no_grad():
            mean, runner.v = brain.act(motion, runner.v)
            sample = mean + torch.exp(log_std) * torch.randn_like(mean)
            data["log_prob"].append(log_probability(mean, log_std, sample))
            features = critic_features(sim)
            data["features"].append(features)
            data["value"].append(critic(features))
            data["teacher"].append(teacher_channels(sim))
        action = torch.tanh(sample)
        info = sim.step(action)
        data["motion"].append(motion)
        data["sample"].append(sample)
        data["reward"].append(reward(sim, info, action, previous))
        data["done"].append(info["done"])
        data["reset"].append(reset_before)
        previous = torch.where(info["done"][:, None], torch.full_like(action, -1.0), action)
        runner.reset_done(info["done"])
        reset_before = info["done"]
    with torch.no_grad():
        last_value = critic(critic_features(sim))
    result = {key: torch.stack(values) for key, values in data.items()}
    result["start_v"] = start_v
    result["last_value"] = last_value
    result["previous"] = previous
    return result


def advantages(data: dict, gamma: float, lam: float) -> tuple[torch.Tensor, torch.Tensor]:
    steps = data["reward"].shape[0]
    advantage = torch.zeros_like(data["reward"])
    running = torch.zeros_like(data["last_value"])
    next_value = data["last_value"]
    for t in reversed(range(steps)):
        alive = (~data["done"][t]).float()
        delta = data["reward"][t] + gamma * next_value * alive - data["value"][t]
        running = delta + gamma * lam * alive * running
        advantage[t] = running
        next_value = data["value"][t]
    return advantage, advantage + data["value"]


def policy_means(brain, data: dict) -> torch.Tensor:
    v = data["start_v"]
    means = []
    for t in range(data["motion"].shape[0]):
        v = torch.where(data["reset"][t][:, None], torch.zeros_like(v), v)
        mean, v = brain.act(data["motion"][t], v)
        means.append(mean)
    return torch.stack(means)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=300)
    parser.add_argument("--batch", type=int, default=128)
    parser.add_argument("--steps", type=int, default=64)
    parser.add_argument("--epochs", type=int, default=3)
    parser.add_argument("--lr", type=float, default=3e-4)
    parser.add_argument("--critic-lr", type=float, default=1e-3)
    parser.add_argument("--gamma", type=float, default=0.99)
    parser.add_argument("--lam", type=float, default=0.95)
    parser.add_argument("--clip", type=float, default=0.2)
    parser.add_argument("--entropy", type=float, default=1e-4)
    parser.add_argument("--teacher-weight", type=float, default=0.3)
    parser.add_argument("--log-std", type=float, default=-1.6)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--resume", default=str(OUT_DIR / "brain_stage_a.pt"))
    parser.add_argument("--out", default=str(OUT_DIR / "brain_stage_b.pt"))
    parser.add_argument("--eval-every", type=int, default=25)
    parser.add_argument("--minutes", type=float, default=0.0)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    device = pick_device(args.device)
    brain = load_brain(args.resume, device)
    runner = Runner(brain, args.batch, seed=args.seed + 100)
    critic = Critic().to(runner.device)
    log_std = nn.Parameter(torch.full((CHANNEL_COUNT,), args.log_std, device=runner.device))
    policy_optimizer = torch.optim.Adam(brain.trainable_parameters() + [log_std], lr=args.lr)
    critic_optimizer = torch.optim.Adam(critic.parameters(), lr=args.critic_lr)
    weights = channel_weights(runner.device)
    previous = torch.full((args.batch, CHANNEL_COUNT), -1.0, device=runner.device)
    started = time.time()

    best = evaluate(brain)
    best_score = best["flight_seconds"] - 20.0 * best["crashed"] - 10.0 * best["lost"]
    save_brain(brain, args.out, {"stage": "B", "iteration": 0, "metrics": best})
    print(f"Stage B on {device}. Start: {format_metrics(best)}", flush=True)

    for iteration in range(1, args.iterations + 1):
        data = collect(runner, critic, log_std.detach(), args.steps, previous)
        previous = data["previous"]
        advantage, returns = advantages(data, args.gamma, args.lam)
        normalized = (advantage - advantage.mean()) / (advantage.std() + 1e-6)
        teacher_weight = args.teacher_weight * max(0.0, 1.0 - iteration / (0.7 * args.iterations))

        for _ in range(args.epochs):
            means = policy_means(brain, data)
            flat_mean = means.reshape(-1, CHANNEL_COUNT)
            log_prob = log_probability(flat_mean, log_std, data["sample"].reshape(-1, CHANNEL_COUNT))
            ratio = torch.exp(log_prob - data["log_prob"].reshape(-1))
            flat_advantage = normalized.reshape(-1)
            surrogate = torch.minimum(
                ratio * flat_advantage, ratio.clamp(1.0 - args.clip, 1.0 + args.clip) * flat_advantage
            )
            teacher_error = ((torch.tanh(means) - data["teacher"]) ** 2 * weights).mean()
            loss = -surrogate.mean() - args.entropy * log_std.sum() + teacher_weight * teacher_error
            policy_optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(brain.trainable_parameters(), 0.5)
            policy_optimizer.step()
            with torch.no_grad():
                log_std.clamp_(-3.0, -0.5)

        flat_features = data["features"].reshape(-1, data["features"].shape[-1])
        flat_returns = returns.reshape(-1)
        for _ in range(args.epochs * 4):
            critic_loss = ((critic(flat_features) - flat_returns) ** 2).mean()
            critic_optimizer.zero_grad()
            critic_loss.backward()
            critic_optimizer.step()

        if iteration % 5 == 0:
            elapsed = time.time() - started
            print(
                f"iteration {iteration} reward per step {float(data['reward'].mean()) / REWARD_SCALE:.3f} "
                f"teacher weight {teacher_weight:.2f} time {elapsed:.0f} s",
                flush=True,
            )
        stop = args.minutes > 0 and time.time() - started > args.minutes * 60
        if iteration % args.eval_every == 0 or iteration == args.iterations or stop:
            metrics = evaluate(brain)
            score = metrics["flight_seconds"] - 20.0 * metrics["crashed"] - 10.0 * metrics["lost"]
            kept = score >= best_score
            if kept:
                best_score = score
                save_brain(brain, args.out, {"stage": "B", "iteration": iteration, "metrics": metrics})
            print(f"  check after {iteration}: {format_metrics(metrics)}{' (saved)' if kept else ''}", flush=True)
        if stop:
            break
    print(f"Best brain saved to {args.out}")


if __name__ == "__main__":
    main()
