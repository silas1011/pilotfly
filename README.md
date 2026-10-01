# PilotFly

A simulated fruit fly brain that flies a drone in the [Uncrashed FPV Drone Simulator](https://store.steampowered.com/app/1682970/).

The brain is built from real data: the wiring map of a real fly brain (FlyWire) and a published model of the fly's eye (flyvis). It looks at the game picture, and its output moves a virtual RadioMaster TX12 radio that Windows and the game see as a normal controller. The fly controls all 8 axes and 24 buttons.

**Status: not finished.** The Windows program, the brain model, the training code and a first trained brain exist. What is missing is the last training stage inside Uncrashed, which needs a Windows PC with the game. The steps are below under "Finish it on Windows".

What the brain can do so far, measured in PilotFly's own simple simulator (64 test flights of 20 seconds, picture as the only input):

| Brain | In the air | Armed by itself | Crashed | Flew away (too high or too far) | Mean height | Forward |
| --- | --- | --- | --- | --- | --- | --- |
| Teacher autopilot (knows the true position) | 19.3 s | 100% | 0% | 0% | 2.9 m | 48 m |
| Fly brain after stage A | 17.1 s | 100% | 0% | 34% | 24.6 m | 23 m |
| Fly brain after stage B (`brain_stage_b.pt`) | 17.2 s | 100% | 0% | 28% | 22.5 m | 59 m |

So the fly arms the drone, takes off and flies forward without crashing, but it does not hold its height: it keeps climbing. It has never flown in Uncrashed. Nobody knows yet how it behaves there.

## How it works

```
game picture ─► two fly eyes ─► flight circuit ─► muscle layer ─► virtual TX12 ─► Uncrashed
 128 x 64       flyvis,          2384 real         825 descending   8 axes,
 grayscale      2 x 721          neurons from      neurons to 32    24 buttons
                ommatidia        FlyWire           channels         (vJoy)
```

1. **Eyes.** The picture is split in the middle. Each half goes to one eye with 721 ommatidia on a hexagon grid. The eye is the flyvis model (Lappalainen et al., Nature 2024): 65 cell types wired like the real optic lobe, with their pretrained weights. It is frozen. The right eye is mirrored, like in the animal.
2. **Flight circuit.** 2384 neurons and 36098 connections cut out of the FlyWire connectome (version 783): 145 lobula plate tangential cells (HS, VS, H1, H2, LPT), 825 descending neurons that they reach, and 1414 neurons on paths of at most two steps between them. Only connections with 5 or more synapses are kept. Which neuron connects to which, whether a connection excites or inhibits, and its relative strength are fixed from the data.
3. **Muscle layer.** One layer from the 825 descending neurons to the 32 controller channels. It stands in for the wing motor system, which is not part of the brain data.

What is trained: one gain, one bias and one time constant per cell type, the muscle layer, and the junction between eye and circuit.

One honest approximation: the junction. flyvis is a generic eye and FlyWire is one specific fly, so they cannot be matched neuron by neuron. The motion cells T4/T5 connect to the tangential cells by cell type and direction layer, which is real. The spatial pattern of that junction is learned.

The fly only sees. A real fly also has halteres (gyroscope organs). The game gives no such data, so the brain has to sense rotation from optic flow.

## Folders

| Folder | What it is |
| --- | --- |
| `runtime/` | The Windows program (C++): virtual TX12, UI with a drawn TX12, runs `brain.onnx`. See `runtime/README.md`. |
| `training/` | Python: eye, circuit, own drone simulator, training, export, and the Uncrashed fine-tune tool. |
| `training/assets/` | The eye weights and the flight circuit, ready to use. |
| `training/checkpoints/` | The brain trained so far. |
| `docs/` | The design document. |

## Training stages

| Stage | Where | What happens |
| --- | --- | --- |
| A | own simulator | The brain copies a classic autopilot that takes off, hovers at 3 m and flies forward. It only sees the picture. |
| B | own simulator | Reinforcement learning (PPO): reward for staying in the air, level, at height and moving forward. |
| C | Uncrashed | Fine-tuning in the real game. **This is the part that still has to be done on a Windows PC.** |

## Finish it on Windows

Written for a PC with an NVIDIA card (tested target: Ryzen 7 7700X, RTX 4060). Run all commands in PowerShell.

### 1. Install

1. [Python 3.12](https://www.python.org/downloads/) (tick "Add python.exe to PATH") and [Git](https://git-scm.com/download/win).
2. vJoy 2.2.2.0 from https://github.com/BrunnerInnovation/vJoy/releases/tag/v2.2.2.0 (`vJoySetup_v2.2.2.0_Win10_Win11.exe`).
3. Open "Configure vJoy". For device 1 tick the axes X, Y, Z, Rx, Ry, Rz, Slider and Dial/Slider2, set 24 buttons and 0 POV hats, then Apply.
4. Get the code and the Python packages:

   ```
   git clone https://github.com/silas1011/pilotfly.git
   cd pilotfly\training
   py -3.12 -m venv .venv
   .venv\Scripts\activate
   pip install torch --index-url https://download.pytorch.org/whl/cu126
   pip install -e .[dev]
   pytest
   ```

   The `torch` line installs the NVIDIA (CUDA) build. If it fails, take the command for your system from https://pytorch.org/get-started/locally/. `pytest` should end with all tests passed.

### 2. Set up Uncrashed

1. Run the game in **windowed or borderless** mode, not exclusive fullscreen. The picture is copied from the screen, so the game window must stay visible and must not be covered.
2. Build and start the PilotFly program once without a brain (see `runtime/README.md`) and click "Start brain + controller". It then moves every control one after another. Use this to set up the controller in the game. If you do not want to build the C++ program yet, skip this and bind by hand.
3. In the game's controller settings pick **vJoy Device** and bind:

   | Game function | vJoy input | TX12 control |
   | --- | --- | --- |
   | Roll | Axis 1 (X) | right stick left/right |
   | Pitch | Axis 2 (Y) | right stick up/down |
   | Throttle | Axis 3 (Z) | left stick up/down |
   | Yaw | Axis 4 (X rotation) | left stick left/right |
   | Arm | Axis 5 (Y rotation), armed when high | switch SE |
   | Flight mode | Axis 6 (Z rotation) | switch SB |
   | Reset | Button 15 | spare button, pressed by the trainer between flights |

4. The brain was trained with the mode switch in the middle meaning **Angle** (self-levelling). If the game cannot put Angle on the middle of an axis, set the game to Angle mode by hand and leave "Flight mode" unbound.
5. Pick an open map with free space around the start point.

These bindings follow the game's patch notes (8 axes and 20 buttons can be bound to actions since version 2.3). The exact menu names were not checked in the game.

### 3. Check the picture

```
python -m pilotfly_train.uncrashed.finetune calibrate
```

Fly by hand for a minute, crash, and let the drone lie still. The tool prints how much the picture changes. While flying the number should be clearly above `0.004`, while lying still clearly below. If not, pass a better value with `--motion-threshold` in the next steps. It should also report about 50 pictures per second.

### 4. See what the brain does before fine-tuning

```
python -m pilotfly_train.uncrashed.finetune fly --checkpoint checkpoints\brain_stage_b.pt
```

The fly now has the controller. **Pause** stops it. Each flight ends when the picture stops moving, then the tool presses Reset and the next flight starts.

You can also download a ready `brain.onnx` of this brain from the Releases page of the repository and use it in the PilotFly program (step 7).

### 5. Fine-tune in Uncrashed (stage C)

```
python -m pilotfly_train.uncrashed.finetune train --checkpoint checkpoints\brain_stage_b.pt
```

- It tries small random changes to the brain, flies each one, and keeps the direction that stayed in the air longer (an evolution strategy). The score of a flight is the number of seconds with a moving picture.
- 40 generations with 8 flights each, at most 20 seconds per flight: about 2 hours. Leave the game window alone during that time.
- Progress is saved after every generation to `out\brain_stage_c.pt`. **Pause** stops and saves.
- To continue later: `--checkpoint out\brain_stage_c.pt`.
- Useful options: `--generations`, `--population`, `--sigma` (size of the random changes, default 0.05), `--episode-seconds`, `--motion-threshold`.

Only gains, biases and the muscle layer are changed in this stage. While training, the Reset button is reserved for the trainer; the fly cannot press it.

The score is simple on purpose: it cannot tell flying well from tumbling through the air. If the fly learns to tumble, lower `--episode-seconds` or train longer in the own simulator first (next section).

### 6. Optional: more training in the own simulator

The RTX 4060 is much faster than the laptop this was started on.

```
python -m pilotfly_train.train_imitation --iterations 800 --resume checkpoints\brain_stage_a.pt --out out\brain_stage_a.pt
python -m pilotfly_train.train_ppo --iterations 600 --resume out\brain_stage_a.pt --out out\brain_stage_b.pt
```

Both print a check line every few iterations, for example `airborne 17.2/20 s, crashed 5%`.

### 7. Run the finished brain in the PilotFly program

```
python -m pilotfly_train.export --checkpoint out\brain_stage_c.pt --out brain.onnx
```

Build the program as described in `runtime/README.md`, copy `brain.onnx` next to `pilotfly.exe`, start the game, then PilotFly, and click "Start brain + controller". The drawn TX12 shows what the fly is doing.

## Practice without the game

`--practice` runs the fine-tune tool against PilotFly's own simulator instead of Uncrashed. It needs no vJoy and works on any system:

```
python -m pilotfly_train.uncrashed.finetune train --practice --generations 2 --checkpoint checkpoints\brain_stage_b.pt
```

## Rebuild the data files

The two files in `training/assets/` can be made again from the sources:

```
pip install -e .[eye]
flyvis download-pretrained
python -m pilotfly_train.extract_eye
python -m pilotfly_train.flywire
```

## What is tested and what is not

Tested on the development Mac:

- The eye gives the same numbers as the original flyvis network (difference about 1e-6).
- The exported `brain.onnx` gives the same numbers as the PyTorch brain (difference about 2e-6) and runs in the C++ program at 50 steps per second on the CPU.
- 26 Python tests and the C++ tests pass.
- Training stages A and B ran in the own simulator (results in the table above).
- The fine-tune tool ran in practice mode against the own simulator.
- The UI, the test pattern and the stop key of the C++ program.

Not tested, because it needs Windows, vJoy, an NVIDIA card or the game:

- Everything that talks to vJoy, in the C++ program and in the Python tool.
- Capturing the Uncrashed window, in both programs, and whether it reaches 50 pictures per second.
- The Windows build of the C++ program, including DirectML.
- Training on CUDA.
- The controller bindings and the Reset behaviour in Uncrashed.
- Stage C itself.

Known weak points:

- The flight score in stage C only measures a moving picture. A drone very high up has an almost still picture and can be counted as crashed.
- The own simulator has no collisions with the pillars and looks much simpler than the game.
- In the own simulator the fly cannot tell its real height, which is why it climbs.

## Sources and credits

- FlyWire connectome v783: Dorkenwald et al., "Neuronal wiring diagram of an adult brain", Nature 2024; Schlegel et al., "Whole-brain annotation and multi-connectome cell typing of Drosophila", Nature 2024. Annotations from https://github.com/flyconnectome/flywire_annotations.
- Connection table with excitatory/inhibitory signs: Shiu et al., "A Drosophila computational brain model reveals sensorimotor processing", Nature 2024, https://github.com/philshiu/Drosophila_brain_model.
- Eye model and weights: Lappalainen et al., "Connectome-constrained networks predict neural activity across the fly visual system", Nature 2024, https://github.com/TuragaLab/flyvis.
- Flight descending neurons: Namiki et al., "A population of descending neurons that regulates the flight motor of Drosophila", Current Biology 2022.
- Controller layout: EdgeTX "Classic Joystick" USB report, https://manual.edgetx.org.
- Libraries: vJoy, ONNX Runtime, Dear ImGui, GLFW, Catch2, PyTorch.

PilotFly is not connected to RadioMaster, EdgeTX or the makers of Uncrashed.

## License

The code is under the MIT license, see `LICENSE`. The files in `training/assets/` are derived from the sources above; cite them if you use the data.
