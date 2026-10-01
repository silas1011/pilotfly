import torch

from pilotfly_train.layout import AXIS_ARM, AXIS_COUNT, AXIS_MODE, AXIS_PITCH, AXIS_THROTTLE, CHANNEL_COUNT
from pilotfly_train.sim import QuadSim
from pilotfly_train.teacher import teacher_channels


def idle(batch: int) -> torch.Tensor:
    channels = torch.full((batch, CHANNEL_COUNT), -1.0)
    channels[:, :AXIS_COUNT] = 0.0
    channels[:, AXIS_THROTTLE] = -1.0
    channels[:, AXIS_ARM] = -1.0
    return channels


def test_disarmed_drone_stays_on_the_ground():
    sim = QuadSim(4, seed=0)
    channels = idle(4)
    channels[:, AXIS_THROTTLE] = 1.0
    for _ in range(100):
        sim.step(channels)
    assert float(sim.position[:, 2].max()) == 0.0
    assert not bool(sim.armed.any())


def test_arming_needs_low_throttle():
    sim = QuadSim(2, seed=0)
    channels = idle(2)
    channels[:, AXIS_ARM] = 1.0
    channels[0, AXIS_THROTTLE] = 1.0
    for _ in range(10):
        sim.step(channels)
    assert not bool(sim.armed[0])
    assert bool(sim.armed[1])


def test_teacher_takes_off_and_flies_forward():
    sim = QuadSim(8, seed=1)
    for _ in range(400):
        info = sim.step(teacher_channels(sim))
    assert not bool(info["crashed"].any())
    assert float((sim.position[:, 2] - 3.0).abs().max()) < 0.5
    assert float(sim.forward_speed().min()) > 1.5


def test_cutting_the_throttle_in_the_air_crashes():
    sim = QuadSim(4, seed=2)
    for _ in range(200):
        sim.step(teacher_channels(sim))
    channels = idle(4)
    channels[:, AXIS_ARM] = 1.0
    channels[:, AXIS_MODE] = -1.0
    channels[:, AXIS_PITCH] = 1.0
    crashed = torch.zeros(4, dtype=torch.bool)
    for _ in range(200):
        crashed |= sim.step(channels)["crashed"]
    assert bool(crashed.all())


def test_disarming_in_flight_is_reported():
    sim = QuadSim(2, seed=3)
    for _ in range(150):
        sim.step(teacher_channels(sim))
    info = sim.step(idle(2))
    assert bool(info["disarmed_in_flight"].all())


def test_render_gives_grayscale_pictures():
    sim = QuadSim(3, seed=4)
    for _ in range(100):
        sim.step(teacher_channels(sim))
    image = sim.render()
    assert image.shape == (3, 1, 64, 128)
    assert float(image.min()) >= 0.0 and float(image.max()) <= 1.0
    assert float(image.std()) > 0.02


def test_reset_only_touches_selected_drones():
    sim = QuadSim(2, seed=5)
    for _ in range(100):
        sim.step(teacher_channels(sim))
    sim.reset(torch.tensor([True, False]))
    assert float(sim.position[0, 2]) == 0.0
    assert float(sim.position[1, 2]) > 1.0
