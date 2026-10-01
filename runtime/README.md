# PilotFly runtime

PilotFly is a virtual RadioMaster TX12 for the Uncrashed FPV simulator. It creates a fake joystick in Windows with 8 axes and 24 buttons, the same layout a real TX12 with EdgeTX sends over USB. The sticks are moved by a trained fly brain (`brain.onnx`) that looks at the game picture. Without a brain file the program uses a built-in test pattern.

Windows lists the device as **"vJoy Device"**, not as "TX12" or "PilotFly". Pick "vJoy Device" in the simulator.

## Windows setup

### 1. Install vJoy

vJoy is the driver that makes the fake joystick. Use the signed version for Windows 10 and 11:

https://github.com/BrunnerInnovation/vJoy/releases/tag/v2.2.2.0

Download `vJoySetup_v2.2.2.0_Win10_Win11.exe`, run it and restart Windows if it asks.

### 2. Configure vJoy device 1

1. Open the Start menu, type `Configure vJoy` and open it.
2. Select tab `1` and tick "Enable vJoy Device".
3. Axes: tick only `X`, `Y`, `Z`, `Rx`, `Ry`, `Rz`, `Slider` and `Dial/Slider2`.
4. Number of buttons: `24`.
5. POV Hat Switch: `0`.
6. Click `Apply`.

### 3. Get the vJoy SDK

The SDK is not part of the installer. Download `SDK.zip` from the same release page and unzip it, for example to `C:\dev\vJoySDK`. Inside is a folder called `SDK` with this layout:

```
SDK\inc\vjoyinterface.h
SDK\inc\public.h
SDK\lib\x64\vJoyInterface.lib
SDK\lib\x64\vJoyInterface.dll
```

`VJOY_SDK_DIR` must point to that `SDK` folder, the one that contains `inc` and `lib`.

### 4. Build in CLion

1. Open the `runtime` folder in CLion.
2. Go to Settings > Build, Execution, Deployment > Toolchains and use the Visual Studio toolchain with architecture `amd64`.
3. Go to Settings > Build, Execution, Deployment > CMake and put this into "CMake options":

   ```
   -DVJOY_SDK_DIR=C:/dev/vJoySDK/SDK
   ```

   Use forward slashes in the path.
4. Reload the CMake project, then build and run the `pilotfly` target.

The build copies `vJoyInterface.dll` next to `pilotfly.exe`. Without `VJOY_SDK_DIR` the program still builds, but only with the dummy controller.

ONNX Runtime, the library that runs the brain, is downloaded automatically by CMake the first time the project is loaded, so the PC needs internet access for that. On Windows the build also copies `onnxruntime.dll`, `onnxruntime_providers_shared.dll` and `DirectML.dll` next to `pilotfly.exe`.

### 5. Check that it works

1. Start PilotFly and click "Start brain + controller".
2. Press Win+R, type `joy.cpl` and press Enter.
3. Select "vJoy Device" and click Properties.
4. The axes and buttons should move with the test pattern.

## The brain and the game picture

Copy a trained `brain.onnx` next to `pilotfly.exe`. If the file is there, PilotFly runs it at the speed stored in the file (normally 50 steps per second, `rate_hz` is ignored then). The brain runs on the graphics card through DirectML, or on the CPU if that is not possible. The status line shows which one is used. If there is no `brain.onnx`, PilotFly uses the test pattern.

The brain sees the game because PilotFly copies the picture of the Uncrashed window from the screen. For that to work:

- Run Uncrashed in windowed or borderless mode. Exclusive fullscreen cannot be captured.
- Keep the game window visible. If another window covers it, the brain sees that window instead.
- Do not minimize the game.
- Start the game first, then click "Check again" in PilotFly.

## pilotfly.ini

The file is optional and sits next to `pilotfly.exe`. Every line is `key = value`.

```
stop_key = Pause
vjoy_device = 1
rate_hz = 100
brain_file = brain.onnx
game_window = Uncrashed
```

| Key | Values | Default |
| --- | --- | --- |
| `stop_key` | `Pause`, `ScrollLock`, `F1` to `F12` | `Pause` |
| `vjoy_device` | `1` to `16` | `1` |
| `rate_hz` | `10` to `1000`, updates per second, only used with the test pattern | `100` |
| `brain_file` | File name of the brain next to `pilotfly.exe`, or a full path | `brain.onnx` |
| `game_window` | Part of the title of the game window, upper and lower case do not matter | `Uncrashed` |

## Stop key

The stop key works even when the simulator is in fullscreen. Pressing it freezes the controller: sticks centred, throttle zero, arm off. It stays frozen until you press the key again.

## Mac build for development

On a Mac there is no vJoy, so the program uses a dummy controller that sends nothing. The game picture is replaced by a dummy picture with moving stripes, and the brain runs on the CPU. The Mac has no Pause key, so put `stop_key = F9` into `pilotfly.ini` next to the built program. Open the `runtime` folder in CLion, or build in the terminal with CMake:

```
cmake -S runtime -B runtime/build && cmake --build runtime/build && ctest --test-dir runtime/build
```
