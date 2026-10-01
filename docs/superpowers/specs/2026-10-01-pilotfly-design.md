# PilotFly design

Date: 2026-10-01

## Goal

A Windows program in which a simulated fruit fly brain, built from the real FlyWire wiring map, flies a drone in the Uncrashed FPV simulator through a virtual RadioMaster TX12. The fly drives all 8 axes and 24 buttons of the controller. The UI offers a Start/Stop button for brain and controller and a drawn TX12 whose sticks, switches, dials and buttons move or light up live.

Success: the fly arms, takes off, hovers for some seconds and drifts forward without crashing right away. Good flying is not promised. Vision-only flight with fixed real wiring is research territory.

## Constraints

- Development on a MacBook (Apple chip, no Docker). The program runs on a Windows PC (i5-13600KF, AMD Vega 56, 32 GB RAM).
- No NVIDIA card, so no CUDA. Training runs on the Mac (PyTorch MPS) or on the PC's CPU. The runtime uses ONNX Runtime with DirectML.
- Uncrashed has no known telemetry output (unconfirmed). The design assumes the game picture is the only feedback.
- No code comments in any source file.

## Overall structure

Two programs that share one file format.

```
TRAINING (Python)
  FlyWire data -> circuit builder -> brain (PyTorch)
                                       ^      |
                    own drone sim -----+      v
                    (physics + fly-eye view)  export -> brain.onnx

RUNTIME (C++, Windows, CMake)
  Uncrashed window -> screen capture -> fly eye (hexagon pixels)
                                             |
                                       brain.onnx (ONNX Runtime + DirectML)
                                             |
                             8 axes + 24 buttons, 11 bit like EdgeTX
                                   |                    |
                        vJoy virtual TX12 -> Uncrashed  UI: TX12 drawing
```

Parts:

- Circuit builder: reads FlyWire files, cuts out the flight circuit, saves one wiring file. Run once.
- Drone sim: quadcopter physics in acro and angle mode plus a simple 3D view rendered straight into fly-eye pixels. Headless and much faster than real time.
- Brain: flyvis eye, connectome circuit, muscle layer. Same code path in training and export.
- Virtual TX12: takes 8 axis values and 24 button states and presents them to Windows as a controller with the layout EdgeTX reports.
- UI: Start/Stop, drawn TX12, status line, stop key.
- Fine-tune bridge: lets the Python trainer use the real game through the runtime.

Windows-only pieces (vJoy, screen capture, DirectML, global hotkey) sit behind small interfaces with dummy versions, so UI and run loop can be built and tested on the Mac.

## Sub-projects

1. Virtual TX12 + UI. C++ runtime with a test pattern instead of a brain.
2. Brain + own sim + training. Ends with a `brain.onnx` that hovers and flies forward in the own sim and is loaded by the runtime.
3. Uncrashed eyes + fine-tune. Screen capture, crash detection, second training phase in the game.

Each sub-project gets its own plan. Sub-projects 2 and 3 get their own spec additions when they start.

## Brain

Senses: vision only. A real fly also has halteres (gyroscope organs). Uncrashed provides no gyro data, so rotation has to be sensed from optic flow, which HS/VS cells do in the real animal. Self-levelling angle mode will therefore be much easier for the fly than acro. The fly chooses the mode itself with the switch.

Stages of the network:

1. Eyes. The game picture is split in the middle, left half to the left eye, right half to the right eye. Each half is resampled onto 721 hexagonal ommatidia, the input format of flyvis.
2. Optic lobe. flyvis (Lappalainen et al., Nature 2024) with pretrained weights, frozen. Output: T4/T5 motion detector activity in four directions.
3. Flight circuit from FlyWire v783. Lobula plate tangential cells (HS, VS and the others), flight descending neurons (DNg02, DNa01, DNa02 and other descending types that receive from the tangential cells), and every neuron on a path of at most 2 steps between them with 5 or more synapses. Expected size: a few thousand neurons. Modelled as a rate network.
4. Muscle layer. One small layer from descending neuron activity to 8 axes and 24 buttons. It stands in for the wing motor system, which the brain-only FlyWire data does not contain.

Fixed from real data: which neurons connect, the sign of each connection (from neurotransmitter predictions), the relative strength (from synapse counts).

Trained: one gain, one time constant and one bias per cell type, and the muscle layer.

Approximation: the junction between stage 2 and stage 3. flyvis is a generic eye, FlyWire is one specific fly, and they cannot be matched neuron by neuron. T4/T5 connect to the tangential cells by cell type and direction layer, which is real. The spatial pattern of that junction is learned.

## Training

- Stage A, imitation. A classic autopilot that knows the true drone state flies in the own sim. The brain sees only the eye picture and learns to produce the same controller outputs, including flipping the arm switch.
- Stage B, reinforcement learning (PPO) in the own sim. Reward for staying airborne, staying level and moving forward. Penalty for crashing and for disarming in flight. Colours, lighting and drone weight are randomised.
- Stage C, fine-tune in Uncrashed. Only gains and muscle layer keep learning. Reset through a controller channel, crash detection from the picture.

## Virtual TX12

Report format: EdgeTX "Classic Joystick", 8 axes with 11 bit resolution and 24 buttons, sent through a vJoy device.

| TX12 part | Channel | Windows axis or button |
|---|---|---|
| Right stick horizontal (roll) | 1 | X |
| Right stick vertical (pitch) | 2 | Y |
| Left stick vertical (throttle) | 3 | Z |
| Left stick horizontal (yaw) | 4 | X rotation |
| SE, 3 positions, arm | 5 | Y rotation |
| SB, 3 positions, flight mode (Acro, Angle, 3D) | 6 | Z rotation |
| Dial S1 | 7 | Slider |
| Dial S2 | 8 | Dial |
| SA, 2 positions | 9 | Button 1 |
| SC, 3 positions | 10 to 11 | Buttons 2 to 3 |
| SD, 2 positions | 12 | Button 4 |
| SF, 3 positions | 13 to 14 | Buttons 5 to 6 |
| 4 trims, 2 buttons each | 15 to 22 | Buttons 7 to 14 |
| Spare | 23 to 32 | Buttons 15 to 24 |

Switch types are taken from the TX12 Mark II quick start guide and the EdgeTX hardware definition `tx12mk2.json`: SA and SD are 2 position buttons, SB, SC, SE and SF are 3 position switches. That SA and SD are momentary is unconfirmed.

A switch on buttons with P positions uses P - 1 buttons. No button pressed is the lowest position, the highest pressed button gives the position.

A channel above centre means the button is pressed, as in EdgeTX.

Uncrashed reads 8 axes and 20 buttons since update 2.3, so buttons 21 to 24 are sent but have no effect in the game. They are spare buttons.

The user installs the vJoy driver once. Windows lists the device as "vJoy Device". Faking the TX12 device name would need a custom driver and is out of scope.

## UI

Dear ImGui with GLFW, one window:

- Start/Stop button for brain and controller.
- Drawn TX12: sticks move, switches flip, dials turn, trim buttons light up. Controls that changed recently are highlighted. The spare buttons are lamps.
- Status line: vJoy state, brain state, game window state, measured loop rate.

Not included: a live brain activity view.

## Stop key

The Pause key by default, changeable in `pilotfly.ini`. It works while Uncrashed has focus. It centres the sticks, sets throttle to minimum, sets the arm switch off, releases all buttons and holds that state until it is pressed again.

## Error handling

- vJoy missing or configured with the wrong axes or buttons: Start is disabled and the status line names the fix.
- Game window not found or brain file missing: same behaviour, no crash.
- Brain slower than the loop rate: the controller keeps the last values and the status line shows the measured rate.

## Tests

- C++ with Catch2: value scaling to 11 bit, layout mapping, safe state, run loop with the dummy controller. Runs on the Mac.
- Python with pytest: drone physics, circuit builder on a tiny fake connectome, PyTorch against ONNX output parity.
- Manual on the Windows PC: `joy.cpl` shows axes and buttons moving, then controller calibration in Uncrashed.

## Resources

- FlyWire connectome v783: https://codex.flywire.ai/api/download and https://zenodo.org/records/10676866
- Neuron annotations: https://github.com/flyconnectome/flywire_annotations
- flyvis: https://pypi.org/project/flyvis
- Whole-brain reference model (Shiu et al. 2024): https://github.com/philshiu/Drosophila_brain_model
- DNg02 descending neurons: https://www.ncbi.nlm.nih.gov/pmc/articles/PMC9206711/
- Lobula plate tangential cells: https://www.ncbi.nlm.nih.gov/pmc/articles/PMC6261601/
- EdgeTX joystick report: https://manual.edgetx.org/edgetx-how-to/joystick-mapping-information-for-game-developers

## As built: brain, training and Uncrashed tools

Changes and details decided while building sub-projects 2 and 3.

- Model contract between training and runtime: `brain.onnx`, opset 17, input `frame` float32 [1, 1, 64, 128] (whole game picture, grayscale 0 to 1), input `state` float32 [1, S] starting at zero, output `channels` [1, 32], output `state_out` [1, S]. One step is 1/50 s. The rate is stored in the model metadata.
- Eye: the flyvis network is rewritten as a convolution on the hexagon grid (65 cell types, 31 x 31 grid, 13 x 13 kernel). It gives the same numbers as flyvis to about 1e-6. Training uses a sparse matrix version of the same computation because it is faster.
- The right eye gets a mirrored picture half, so that front-to-back motion excites the same cell types in both eyes.
- Flight circuit from FlyWire v783: 2384 neurons (145 tangential cells, 1414 intermediate, 825 descending), 36098 connections with 5 or more synapses, 886 cell types. The connection table with signs comes from the repository of Shiu et al. because Zenodo was not reachable.
- Connection weights are the signed synapse counts, scaled per receiving neuron so that the inputs inside the circuit sum to about 1. Without that scaling almost no signal reached the descending neurons.
- Junction from eye to circuit: HS cells listen to T4a/T5a, H1 and H2 to T4b/T5b, VS cells to T4d/T5d, other tangential cells to all four directions. The spatial weighting is learned.
- Own simulator: batched quadcopter physics with acro and angle mode, an arm switch that only arms at low throttle, ground, pillars and sky rendered by ray casting. Thrust to weight ratio, drag, camera tilt, field of view, textures and lighting are randomised.
- Stage C uses an evolution strategy instead of gradient learning, because the game gives no reward signal. The score of a flight is the time with a moving picture. The Python tool talks to vJoy and captures the screen itself, so no bridge to the C++ program is needed.
- The C++ program captures the game by copying the window area from the screen (GDI). This works for windowed and borderless mode only.
- ONNX Runtime with DirectML is used on Windows, with a fallback to the CPU.
