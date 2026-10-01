import torch

from pilotfly_train.eye import FlyEye, hex_centers, sampling_matrix


def test_hex_lattice_has_721_ommatidia():
    u, v, x, y = hex_centers(15)
    assert len(u) == 721
    assert x.min() >= -1.0 and x.max() <= 1.0
    assert y.min() >= -1.0 and y.max() <= 1.0


def test_sampling_matrix_rows_are_averages():
    matrix = sampling_matrix(15, 64, 64)
    assert matrix.shape == (721, 64 * 64)
    assert abs(float(matrix.sum(axis=1).min()) - 1.0) < 1e-5
    assert abs(float(matrix.sum(axis=1).max()) - 1.0) < 1e-5


def test_sparse_and_convolution_steps_agree():
    torch.manual_seed(0)
    eye = FlyEye()
    state_a = eye.settle(2, seconds=0.2)
    state_b = state_a.clone()
    for step in range(10):
        hexals = torch.rand(2, eye.n_hex)
        state_a = eye.step(hexals, state_a)
        state_b = eye.step_sparse(hexals, state_b)
    assert float((state_a - state_b).abs().max()) < 1e-4


def test_motion_cells_respond_to_moving_pattern():
    eye = FlyEye()
    state = eye.settle(1)
    rest = eye.motion(state)
    _, _, x, _ = hex_centers(eye.extent)
    position = torch.from_numpy(x)[None]
    for step in range(30):
        hexals = 0.5 + 0.4 * torch.sin(8.0 * position - step * 0.6)
        state = eye.step(hexals, state)
    response = (eye.motion(state) - rest).abs().mean()
    assert float(response) > 0.01
