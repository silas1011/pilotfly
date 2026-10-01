import math
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

from pilotfly_train.eye import EYE_DT, FlyEye, sampling_matrix
from pilotfly_train.flywire import CIRCUIT_PATH, ROLE_DN, ROLE_LPTC
from pilotfly_train.layout import CHANNEL_COUNT, IMAGE_HEIGHT, IMAGE_WIDTH

MIN_TAU = EYE_DT
MAX_TAU = 0.5
VOLTAGE_LIMIT = 20.0
MAX_INPUT_SCALE = 10.0
REST_BIAS = 0.3
JUNCTION_GAIN = 10.0


class Junction(nn.Module):
    def __init__(self, direction_mask: np.ndarray, side_left: np.ndarray, n_hex: int):
        super().__init__()
        n = direction_mask.shape[0]
        mask = np.concatenate([direction_mask, direction_mask], axis=1).astype(np.float32)
        self.register_buffer("mask", torch.from_numpy(mask))
        self.register_buffer("left", torch.from_numpy(side_left.astype(np.float32))[None, :, None])
        self.strength = nn.Parameter(torch.zeros(n, mask.shape[1]))
        self.spatial = nn.Parameter(torch.zeros(n, n_hex))

    def forward(self, motion_left: torch.Tensor, motion_right: torch.Tensor) -> torch.Tensor:
        spatial = torch.softmax(self.spatial, dim=1)
        pooled_left = (motion_left @ spatial.t()).transpose(1, 2)
        pooled_right = (motion_right @ spatial.t()).transpose(1, 2)
        pooled = self.left * pooled_left + (1.0 - self.left) * pooled_right
        strength = F.softplus(self.strength) * self.mask
        return JUNCTION_GAIN * (pooled * strength).sum(dim=2)


class FlightCircuit(nn.Module):
    def __init__(self, circuit_path: Path = CIRCUIT_PATH, dt: float = EYE_DT):
        super().__init__()
        data = np.load(circuit_path)
        self.dt = dt
        self.n = len(data["role"])
        role = data["role"]
        self.n_types = len(data["type_names"])
        self.register_buffer("neuron_type", torch.from_numpy(data["neuron_type"]))
        self.register_buffer("lptc_index", torch.from_numpy(np.where(role == ROLE_LPTC)[0]))
        self.register_buffer("dn_index", torch.from_numpy(np.where(role == ROLE_DN)[0]))
        self.direction_mask = data["lptc_direction_mask"]
        self.lptc_side_left = data["side_left"][role == ROLE_LPTC]
        indices = np.stack([data["edge_post"], data["edge_pre"]])
        included = np.zeros(self.n, dtype=np.float32)
        np.add.at(included, data["edge_post"], np.abs(data["edge_weight"]))
        scale = 1.0 / np.maximum(included, 1.0 / MAX_INPUT_SCALE)
        weight = data["edge_weight"] * scale[data["edge_post"]]
        self.register_buffer("edge_index", torch.from_numpy(indices))
        self.register_buffer("edge_weight", torch.from_numpy(weight.astype(np.float32)))
        self._sparse = None
        scatter = np.zeros((int((role == ROLE_LPTC).sum()), self.n), dtype=np.float32)
        scatter[np.arange(scatter.shape[0]), np.where(role == ROLE_LPTC)[0]] = 1.0
        self.register_buffer("lptc_scatter", torch.from_numpy(scatter))
        self.gain = nn.Parameter(torch.full((self.n_types,), math.log(math.e - 1.0)))
        self.bias = nn.Parameter(torch.full((self.n_types,), REST_BIAS))
        self.tau = nn.Parameter(torch.zeros(self.n_types))

    @property
    def n_lptc(self) -> int:
        return len(self.lptc_index)

    @property
    def n_dn(self) -> int:
        return len(self.dn_index)

    def dense_weights(self) -> torch.Tensor:
        dense = torch.zeros(self.n, self.n, device=self.edge_weight.device)
        dense.index_put_((self.edge_index[0], self.edge_index[1]), self.edge_weight, accumulate=True)
        return dense

    def sparse_weights(self) -> torch.Tensor:
        if self._sparse is None or self._sparse.device != self.edge_weight.device:
            self._sparse = torch.sparse_coo_tensor(self.edge_index, self.edge_weight, (self.n, self.n)).coalesce()
        return self._sparse

    def neuron_params(self) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        gain = F.softplus(self.gain)[self.neuron_type]
        bias = self.bias[self.neuron_type]
        tau = MIN_TAU + (MAX_TAU - MIN_TAU) * torch.sigmoid(self.tau - 2.0)
        rate = (self.dt / tau)[self.neuron_type]
        return gain, bias, rate

    def step(self, v: torch.Tensor, lptc_input: torch.Tensor, dense: torch.Tensor | None = None) -> torch.Tensor:
        gain, bias, rate = self.neuron_params()
        activity = torch.relu(v) * gain
        if dense is not None:
            recurrent = activity @ dense.t()
        else:
            recurrent = torch.sparse.mm(self.sparse_weights(), activity.t()).t()
        external = lptc_input @ self.lptc_scatter
        v = v + rate * (-v + recurrent + bias + external)
        return v.clamp(-VOLTAGE_LIMIT, VOLTAGE_LIMIT)

    def descending(self, v: torch.Tensor) -> torch.Tensor:
        return torch.relu(v[:, self.dn_index])


class FlyBrain(nn.Module):
    def __init__(self, circuit_path: Path = CIRCUIT_PATH):
        super().__init__()
        self.eye = FlyEye()
        for parameter in self.eye.parameters():
            parameter.requires_grad_(False)
        half_width = IMAGE_WIDTH // 2
        matrix = sampling_matrix(self.eye.extent, IMAGE_HEIGHT, half_width)
        self.register_buffer("sampler", torch.from_numpy(matrix))
        self.circuit = FlightCircuit(circuit_path)
        self.junction = Junction(self.circuit.direction_mask, self.circuit.lptc_side_left, self.eye.n_hex)
        self.muscle = nn.Linear(self.circuit.n_dn, CHANNEL_COUNT)
        nn.init.normal_(self.muscle.weight, std=0.01)
        nn.init.zeros_(self.muscle.bias)
        with torch.no_grad():
            rest_state = self.eye.settle(1)
            self.register_buffer("eye_rest_state", rest_state)
            self.register_buffer("motion_rest", self.eye.motion(rest_state))

    @property
    def eye_state_size(self) -> int:
        return self.eye.state_size

    @property
    def state_size(self) -> int:
        return 2 * self.eye.state_size + self.circuit.n

    def trainable_parameters(self) -> list[nn.Parameter]:
        return [p for p in self.parameters() if p.requires_grad]

    def initial_eye_state(self, batch: int) -> torch.Tensor:
        return self.eye_rest_state.expand(batch, -1).repeat(1, 2).clone()

    def initial_circuit_state(self, batch: int) -> torch.Tensor:
        return torch.zeros(batch, self.circuit.n, device=self.sampler.device)

    def initial_state(self, batch: int) -> torch.Tensor:
        return torch.zeros(batch, self.state_size, device=self.sampler.device)

    def hexals(self, frame: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor]:
        batch = frame.shape[0]
        half = IMAGE_WIDTH // 2
        left = frame[:, 0, :, :half].reshape(batch, -1)
        right = torch.flip(frame[:, 0, :, half:], dims=[2]).reshape(batch, -1)
        return left @ self.sampler.t(), right @ self.sampler.t()

    def see(self, frame: torch.Tensor, eye_state: torch.Tensor, sparse: bool = False) -> tuple[torch.Tensor, torch.Tensor]:
        size = self.eye.state_size
        left_hex, right_hex = self.hexals(frame)
        step = self.eye.step_sparse if sparse else self.eye.step
        left_state = step(left_hex, eye_state[:, :size])
        right_state = step(right_hex, eye_state[:, size:])
        left_motion = torch.relu(self.eye.motion(left_state) - self.motion_rest)
        right_motion = torch.relu(self.eye.motion(right_state) - self.motion_rest)
        motion = torch.stack([left_motion, right_motion], dim=1)
        return motion, torch.cat([left_state, right_state], dim=1)

    def act(self, motion: torch.Tensor, v: torch.Tensor, dense: torch.Tensor | None = None) -> tuple[torch.Tensor, torch.Tensor]:
        lptc_input = self.junction(motion[:, 0], motion[:, 1])
        v = self.circuit.step(v, lptc_input, dense)
        return self.muscle(self.circuit.descending(v)), v

    def forward(
        self, frame: torch.Tensor, state: torch.Tensor, dense: torch.Tensor | None = None
    ) -> tuple[torch.Tensor, torch.Tensor]:
        eye_size = 2 * self.eye.state_size
        rest = self.eye_rest_state.repeat(1, 2)
        if dense is None:
            dense = self.circuit.dense_weights()
        motion, eye_state = self.see(frame, state[:, :eye_size] + rest)
        raw, v = self.act(motion, state[:, eye_size:], dense)
        return torch.tanh(raw), torch.cat([eye_state - rest, v], dim=1)
