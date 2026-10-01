import numpy as np
import torch

from pilotfly_train.brain import FlyBrain
from pilotfly_train.export import check_parity, export_brain
from pilotfly_train.layout import CHANNEL_COUNT, IMAGE_HEIGHT, IMAGE_WIDTH
from pilotfly_train.runner import load_brain, save_brain


def random_brain(seed: int = 0) -> FlyBrain:
    torch.manual_seed(seed)
    brain = FlyBrain()
    with torch.no_grad():
        brain.muscle.weight.normal_(0.0, 0.5)
        brain.junction.strength.normal_(0.0, 1.0)
        brain.circuit.bias.normal_(0.0, 0.3)
    return brain


def test_brain_uses_the_real_circuit():
    brain = FlyBrain()
    assert brain.circuit.n == 2384
    assert brain.circuit.n_lptc == 145
    assert brain.circuit.n_dn == 825
    assert brain.eye.n_hex == 721
    frozen = {name for name, parameter in brain.named_parameters() if not parameter.requires_grad}
    assert all(name.startswith("eye.") for name in frozen)


def test_forward_gives_32_channels_in_range():
    brain = random_brain()
    state = brain.initial_state(2)
    frame = torch.rand(2, 1, IMAGE_HEIGHT, IMAGE_WIDTH)
    with torch.no_grad():
        channels, state = brain(frame, state)
    assert channels.shape == (2, CHANNEL_COUNT)
    assert state.shape == (2, brain.state_size)
    assert float(channels.abs().max()) <= 1.0


def test_training_path_matches_export_path():
    brain = random_brain(1)
    state = brain.initial_state(1)
    eye_state = brain.initial_eye_state(1)
    v = brain.initial_circuit_state(1)
    with torch.no_grad():
        for step in range(5):
            frame = torch.rand(1, 1, IMAGE_HEIGHT, IMAGE_WIDTH)
            channels, state = brain(frame, state)
            motion, eye_state = brain.see(frame, eye_state, sparse=True)
            raw, v = brain.act(motion, v)
    assert float((torch.tanh(raw) - channels).abs().max()) < 1e-4


def test_checkpoint_round_trip(tmp_path):
    brain = random_brain(2)
    path = tmp_path / "brain.pt"
    save_brain(brain, path)
    loaded = load_brain(path)
    assert torch.equal(loaded.muscle.weight, brain.muscle.weight)
    assert torch.equal(loaded.circuit.gain, brain.circuit.gain)


def test_exported_brain_matches_pytorch(tmp_path):
    import onnx

    brain = random_brain(3)
    path = export_brain(brain, tmp_path / "brain.onnx")
    model = onnx.load(str(path))
    assert model.ir_version <= 9
    assert model.opset_import[0].version == 17
    assert [i.name for i in model.graph.input] == ["frame", "state"]
    assert [o.name for o in model.graph.output] == ["channels", "state_out"]
    metadata = {entry.key: entry.value for entry in model.metadata_props}
    assert metadata["pilotfly_rate_hz"] == "50"
    assert check_parity(brain, path, steps=8) < 1e-3
