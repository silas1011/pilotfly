import argparse
import time

import torch

from pilotfly_train.layout import AXIS_COUNT, CHANNEL_COUNT
from pilotfly_train.runner import OUT_DIR, Runner, evaluate, format_metrics, load_brain, pick_device, save_brain
from pilotfly_train.teacher import teacher_channels

MIN_VARIANCE = 0.02
EARLY_SECONDS = 2.0
EARLY_WEIGHT = 4.0
BUTTON_SHARE = 0.1


def channel_weights(device) -> torch.Tensor:
    weights = torch.full((CHANNEL_COUNT,), 0.2, device=device)
    weights[:AXIS_COUNT] = 1.0
    weights[:6] = 3.0
    return weights / weights.mean()


def spread_weights(target: torch.Tensor, weights: torch.Tensor) -> torch.Tensor:
    variance = target.reshape(-1, CHANNEL_COUNT).var(dim=0)
    spread = weights / (variance + MIN_VARIANCE)
    spread[AXIS_COUNT:] = weights[AXIS_COUNT:] / MIN_VARIANCE * BUTTON_SHARE
    return spread


def collect(runner: Runner, steps: int, teacher_share: float) -> dict:
    brain, sim = runner.brain, runner.sim
    motions, targets, resets, early = [], [], [], []
    start_v = runner.v.clone()
    use_teacher = torch.rand(runner.batch, device=runner.device) < teacher_share
    reset_before = torch.zeros(runner.batch, dtype=torch.bool, device=runner.device)
    for _ in range(steps):
        motion = runner.observe()
        with torch.no_grad():
            raw, runner.v = brain.act(motion, runner.v)
        target = teacher_channels(sim)
        early.append(sim.time < EARLY_SECONDS)
        action = torch.where(use_teacher[:, None], target, torch.tanh(raw))
        info = sim.step(action)
        motions.append(motion)
        targets.append(target)
        resets.append(reset_before)
        runner.reset_done(info["done"])
        reset_before = info["done"]
    return {
        "motion": torch.stack(motions),
        "target": torch.stack(targets),
        "reset": torch.stack(resets),
        "early": torch.stack(early),
        "start_v": start_v,
    }


def imitation_loss(brain, data: dict, weights: torch.Tensor) -> torch.Tensor:
    v = data["start_v"]
    total = 0.0
    steps = data["motion"].shape[0]
    weights = spread_weights(data["target"], weights)
    for t in range(steps):
        v = torch.where(data["reset"][t][:, None], torch.zeros_like(v), v)
        raw, v = brain.act(data["motion"][t], v)
        error = (torch.tanh(raw) - data["target"][t]) ** 2
        emphasis = 1.0 + (EARLY_WEIGHT - 1.0) * data["early"][t].float()
        total = total + (error * weights * emphasis[:, None]).mean()
    return total / steps


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=600)
    parser.add_argument("--batch", type=int, default=128)
    parser.add_argument("--steps", type=int, default=32)
    parser.add_argument("--updates", type=int, default=3)
    parser.add_argument("--lr", type=float, default=3e-3)
    parser.add_argument("--device", default="auto")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--resume", default=None)
    parser.add_argument("--out", default=str(OUT_DIR / "brain_stage_a.pt"))
    parser.add_argument("--eval-every", type=int, default=100)
    parser.add_argument("--minutes", type=float, default=0.0)
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    device = pick_device(args.device)
    brain = load_brain(args.resume, device)
    runner = Runner(brain, args.batch, seed=args.seed)
    optimizer = torch.optim.Adam(brain.trainable_parameters(), lr=args.lr)
    weights = channel_weights(runner.device)
    started = time.time()
    best_score = -1.0e9
    print(f"Stage A on {device}: {args.iterations} iterations, {args.batch} drones, {args.steps} steps each")

    for iteration in range(1, args.iterations + 1):
        progress = iteration / args.iterations
        teacher_share = max(0.2, 1.0 - 1.6 * progress)
        data = collect(runner, args.steps, teacher_share)
        for _ in range(args.updates):
            loss = imitation_loss(brain, data, weights)
            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(brain.trainable_parameters(), 1.0)
            optimizer.step()
        if iteration % 10 == 0:
            elapsed = time.time() - started
            print(f"iteration {iteration} loss {loss.item():.4f} teacher share {teacher_share:.2f} time {elapsed:.0f} s", flush=True)
        stop = args.minutes > 0 and time.time() - started > args.minutes * 60
        if iteration % args.eval_every == 0 or iteration == args.iterations or stop:
            metrics = evaluate(brain)
            score = metrics["flight_seconds"] - 20.0 * metrics["crashed"] - 10.0 * metrics["lost"]
            kept = score >= best_score
            if kept:
                best_score = score
                save_brain(brain, args.out, {"stage": "A", "iteration": iteration, "metrics": metrics})
            print(f"  check after {iteration}: {format_metrics(metrics)}{' (saved)' if kept else ''}", flush=True)
        if stop:
            break
    print(f"Best brain saved to {args.out}")


if __name__ == "__main__":
    main()
