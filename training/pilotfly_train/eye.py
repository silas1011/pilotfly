import warnings
from pathlib import Path

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

warnings.filterwarnings("ignore", message=".*Sparse.*")
warnings.filterwarnings("ignore", message=".*sparse.*")

ASSET_PATH = Path(__file__).resolve().parent.parent / "assets" / "flyvis_eye.npz"
MOTION_TYPES = ["T4a", "T4b", "T4c", "T4d", "T5a", "T5b", "T5c", "T5d"]
EYE_DT = 0.02


def hex_centers(extent: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    us, vs = [], []
    for u in range(-extent, extent + 1):
        for v in range(-extent, extent + 1):
            if abs(u + v) <= extent:
                us.append(u)
                vs.append(v)
    u = np.array(us)
    v = np.array(vs)
    x = (u + v / 2.0) / (extent + 0.5)
    y = (v * np.sqrt(3.0) / 2.0) / (extent * np.sqrt(3.0) / 2.0 + 0.5)
    return u, v, x.astype(np.float32), y.astype(np.float32)


def sampling_matrix(extent: int, height: int, width: int, sigma_pixels: float = 1.0) -> np.ndarray:
    _, _, x, y = hex_centers(extent)
    px = (x + 1.0) * 0.5 * (width - 1)
    py = (y + 1.0) * 0.5 * (height - 1)
    grid_y, grid_x = np.mgrid[0:height, 0:width]
    dx = grid_x[None, :, :] - px[:, None, None]
    dy = grid_y[None, :, :] - py[:, None, None]
    weights = np.exp(-(dx * dx + dy * dy) / (2.0 * sigma_pixels * sigma_pixels))
    weights = weights.reshape(len(px), height * width)
    weights /= weights.sum(axis=1, keepdims=True)
    return weights.astype(np.float32)


class FlyEye(nn.Module):
    def __init__(self, asset_path: Path = ASSET_PATH, dt: float = EYE_DT):
        super().__init__()
        data = np.load(asset_path)
        self.type_names = [str(name) for name in data["type_names"]]
        self.extent = int(data["extent"])
        self.size = 2 * self.extent + 1
        self.n_types = len(self.type_names)
        input_types = {str(name) for name in data["input_types"]}
        tau = np.maximum(data["tau"], dt)

        self.register_buffer("kernel", torch.from_numpy(data["kernel"]))
        self.register_buffer("mask", torch.from_numpy(data["mask"])[None])
        self.register_buffer("bias", torch.from_numpy(data["bias"])[None, :, None, None])
        self.register_buffer("rate", torch.from_numpy((dt / tau).astype(np.float32))[None, :, None, None])
        input_mask = np.array([1.0 if name in input_types else 0.0 for name in self.type_names], dtype=np.float32)
        self.register_buffer("input_mask", torch.from_numpy(input_mask)[None, :, None, None])

        u, v, _, _ = hex_centers(self.extent)
        flat = (u + self.extent) * self.size + (v + self.extent)
        self.register_buffer("hex_index", torch.from_numpy(flat.astype(np.int64)))
        placement = np.zeros((len(u), self.size * self.size), dtype=np.float32)
        placement[np.arange(len(u)), flat] = 1.0
        self.register_buffer("placement", torch.from_numpy(placement))
        self.n_hex = len(u)
        self.motion_index = [self.type_names.index(name) for name in MOTION_TYPES]
        self.padding = self.kernel.shape[-1] // 2

    @property
    def state_size(self) -> int:
        return self.n_types * self.size * self.size

    def initial_state(self, batch: int) -> torch.Tensor:
        state = (self.bias * self.mask).expand(batch, -1, -1, -1)
        return state.reshape(batch, -1).clone()

    def hex_to_grid(self, hexals: torch.Tensor) -> torch.Tensor:
        return (hexals @ self.placement).reshape(-1, 1, self.size, self.size)

    def step(self, hexals: torch.Tensor, state: torch.Tensor) -> torch.Tensor:
        batch = hexals.shape[0]
        v = state.reshape(batch, self.n_types, self.size, self.size)
        stimulus = self.hex_to_grid(hexals) * self.input_mask
        current = F.conv2d(torch.relu(v) * self.mask, self.kernel, padding=self.padding)
        v = v + self.rate * (-v + self.bias + current + stimulus)
        v = v * self.mask
        return v.reshape(batch, -1)

    def sparse_matrix(self) -> torch.Tensor:
        cached = getattr(self, "_sparse", None)
        if cached is not None and cached.device == self.kernel.device:
            return cached
        size = self.size
        kernel = self.kernel.cpu()
        mask = self.mask[0].cpu()
        radius = self.padding
        target, source, row, column = torch.nonzero(kernel, as_tuple=True)
        ys, xs = torch.meshgrid(torch.arange(size), torch.arange(size), indexing="ij")
        ys, xs = ys.reshape(1, -1), xs.reshape(1, -1)
        source_y = ys + row[:, None] - radius
        source_x = xs + column[:, None] - radius
        inside = (source_y >= 0) & (source_y < size) & (source_x >= 0) & (source_x < size)
        safe_y = source_y.clamp(0, size - 1)
        safe_x = source_x.clamp(0, size - 1)
        valid = inside & (mask[target[:, None], ys, xs] > 0) & (mask[source[:, None], safe_y, safe_x] > 0)
        rows = (target[:, None] * size * size + ys * size + xs)[valid]
        columns = (source[:, None] * size * size + safe_y * size + safe_x)[valid]
        values = kernel[target, source, row, column][:, None].expand(-1, size * size)[valid]
        n = self.state_size
        matrix = torch.sparse_coo_tensor(torch.stack([rows, columns]), values, (n, n)).coalesce()
        self._sparse = matrix.to_sparse_csr().to(self.kernel.device)
        return self._sparse

    def flat_constants(self) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
        cached = getattr(self, "_flat", None)
        if cached is not None and cached[0].device == self.kernel.device:
            return cached
        shape = (1, self.n_types, self.size, self.size)
        rate = self.rate.expand(shape).reshape(1, -1).clone()
        bias = self.bias.expand(shape).reshape(1, -1).clone()
        mask = self.mask.reshape(1, -1).clone()
        offsets = torch.nonzero(self.input_mask.reshape(-1) > 0).reshape(-1) * self.size * self.size
        index = (offsets[:, None] + self.hex_index[None, :]).reshape(-1)
        self._flat = (rate, bias, mask, index)
        return self._flat

    def step_sparse(self, hexals: torch.Tensor, state: torch.Tensor) -> torch.Tensor:
        rate, bias, mask, index = self.flat_constants()
        repeats = index.shape[0] // self.n_hex
        current = (self.sparse_matrix() @ torch.relu(state).t().contiguous()).t()
        drive = -state + bias + current
        drive = drive.index_add(1, index, hexals.repeat(1, repeats))
        return (state + rate * drive) * mask

    def step_sparse_parallel(self, hexals: torch.Tensor, state: torch.Tensor, pool, workers: int) -> torch.Tensor:
        self.sparse_matrix()
        self.flat_constants()
        hex_chunks = hexals.chunk(workers)
        state_chunks = state.chunk(workers)
        results = pool.map(lambda pair: self.step_sparse(pair[0], pair[1]), zip(hex_chunks, state_chunks))
        return torch.cat(list(results), dim=0)

    def motion(self, state: torch.Tensor) -> torch.Tensor:
        batch = state.shape[0]
        v = state.reshape(batch, self.n_types, self.size * self.size)
        v = v[:, self.motion_index, :]
        return v[:, :, self.hex_index]

    def settle(self, batch: int, seconds: float = 1.0, value: float = 0.5) -> torch.Tensor:
        state = self.initial_state(batch).to(self.kernel.device)
        grey = torch.full((batch, self.n_hex), value, device=self.kernel.device)
        for _ in range(int(seconds / EYE_DT)):
            state = self.step(grey, state)
        return state
