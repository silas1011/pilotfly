import argparse
from pathlib import Path

import numpy as np
import onnx
import torch
from torch import nn

from pilotfly_train.brain import FlyBrain
from pilotfly_train.layout import IMAGE_HEIGHT, IMAGE_WIDTH, RATE_HZ
from pilotfly_train.runner import OUT_DIR, load_brain

OPSET = 17
IR_VERSION = 9


class ExportBrain(nn.Module):
    def __init__(self, brain: FlyBrain):
        super().__init__()
        self.brain = brain
        self.register_buffer("dense", brain.circuit.dense_weights())

    def forward(self, frame: torch.Tensor, state: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        return self.brain(frame, state, self.dense)


def export_brain(brain: FlyBrain, path: Path) -> Path:
    brain = brain.to("cpu").eval()
    wrapper = ExportBrain(brain).eval()
    frame = torch.full((1, 1, IMAGE_HEIGHT, IMAGE_WIDTH), 0.5)
    state = torch.zeros(1, brain.state_size)
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        wrapper,
        (frame, state),
        str(path),
        input_names=["frame", "state"],
        output_names=["channels", "state_out"],
        opset_version=OPSET,
        do_constant_folding=True,
        dynamo=False,
    )
    model = onnx.load(str(path))
    model.ir_version = IR_VERSION
    del model.metadata_props[:]
    for key, value in {"pilotfly_rate_hz": str(RATE_HZ), "pilotfly_state_size": str(brain.state_size)}.items():
        entry = model.metadata_props.add()
        entry.key = key
        entry.value = value
    onnx.checker.check_model(model)
    onnx.save(model, str(path))
    return path


def check_parity(brain: FlyBrain, path: Path, steps: int = 20) -> float:
    import onnxruntime

    session = onnxruntime.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    brain = brain.to("cpu").eval()
    generator = torch.Generator().manual_seed(0)
    torch_state = torch.zeros(1, brain.state_size)
    onnx_state = np.zeros((1, brain.state_size), dtype=np.float32)
    worst = 0.0
    base = torch.rand(1, 1, IMAGE_HEIGHT, IMAGE_WIDTH, generator=generator)
    for step in range(steps):
        frame = torch.roll(base, shifts=step * 2, dims=3)
        with torch.no_grad():
            torch_channels, torch_state = brain(frame, torch_state)
        onnx_channels, onnx_state = session.run(
            ["channels", "state_out"], {"frame": frame.numpy(), "state": onnx_state}
        )
        worst = max(worst, float(np.abs(onnx_channels - torch_channels.numpy()).max()))
        worst = max(worst, float(np.abs(onnx_state - torch_state.numpy()).max()))
    return worst


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", default=str(OUT_DIR / "brain_stage_b.pt"))
    parser.add_argument("--out", default=str(OUT_DIR / "brain.onnx"))
    args = parser.parse_args()
    brain = load_brain(args.checkpoint)
    path = export_brain(brain, Path(args.out))
    difference = check_parity(brain, path)
    size = path.stat().st_size / 1e6
    print(f"Saved {path} ({size:.1f} MB). Largest difference between PyTorch and ONNX: {difference:.2e}")
    if difference > 1e-3:
        raise SystemExit("The exported brain does not match the PyTorch brain.")


if __name__ == "__main__":
    main()
