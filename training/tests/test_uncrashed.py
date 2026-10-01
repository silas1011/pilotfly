import argparse

import numpy as np
import torch

from pilotfly_train.brain import FlyBrain
from pilotfly_train.layout import AXIS_ARM, AXIS_THROTTLE, CHANNEL_COUNT
from pilotfly_train.uncrashed.capture import to_brain_frame
from pilotfly_train.uncrashed.finetune import Pilot, rank_weights, read_vector, run_episode, write_vector
from pilotfly_train.uncrashed.game import SimGame
from pilotfly_train.uncrashed.vjoy import axis_to_range, button_mask, safe_channels


def episode_args(**overrides):
    values = dict(
        episode_seconds=3.0, motion_threshold=0.004, takeoff_seconds=0.5, crash_seconds=1.5,
        idle_seconds=1.0, stop_key="Pause",
    )
    values.update(overrides)
    return argparse.Namespace(**values)


def test_axis_scaling_matches_the_runtime():
    assert axis_to_range(-1.0, 1, 32768) == 1
    assert axis_to_range(1.0, 1, 32768) == 32768
    assert axis_to_range(0.0, 0, 2047) == 1024
    assert axis_to_range(float("nan"), 0, 2047) == 1024
    assert axis_to_range(7.0, 0, 2047) == 2047


def test_button_mask_uses_channels_above_centre():
    channels = np.full(CHANNEL_COUNT, -1.0)
    channels[8] = 0.5
    channels[31] = 0.1
    channels[9] = 0.0
    assert button_mask(channels) == (1 << 0) | (1 << 23)


def test_safe_channels_disarm_and_cut_throttle():
    channels = safe_channels()
    assert channels[AXIS_THROTTLE] == -1.0
    assert channels[AXIS_ARM] == -1.0
    assert button_mask(channels) == 0


def test_screen_pixels_become_a_brain_frame():
    pixels = np.zeros((200, 400, 4), dtype=np.uint8)
    pixels[:, 200:, :3] = 255
    frame = to_brain_frame(pixels)
    assert frame.shape == (1, 1, 64, 128)
    assert float(frame[0, 0, :, :60].max()) == 0.0
    assert float(frame[0, 0, :, 70:].min()) == 1.0


def test_rank_weights_are_centred():
    weights = rank_weights(np.array([3.0, 1.0, 2.0]))
    assert list(weights) == [0.5, -0.5, 0.0]


def test_parameter_vector_round_trip():
    brain = FlyBrain()
    vector = read_vector(brain)
    write_vector(brain, vector + 1.0)
    assert torch.allclose(read_vector(brain), vector + 1.0)


def test_untrained_brain_never_takes_off_in_practice_game():
    game = SimGame()
    pilot = Pilot(FlyBrain(), "cpu")
    result = run_episode(game, pilot, episode_args())
    assert result["reason"] == "never started moving"
    assert result["score"] < 0.5
