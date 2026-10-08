# SeeSharp — Computer Vision Engine (C++)

## Overview
Modular C++ computer vision application: OpenCV processing pipeline with a
JSON-configurable set of recognition algorithms. Runs on desktop (camera or
video file, GUI), MTV3 (RV1126: frames from the camera daemon's shared-memory
ring, headless, NN inference on the NPU via RKNN) and Raspberry Pi Compute
Module 5 (V4L2/libcamera camera, headless, ONNX inference via cv::dnn).

Architecture mirrors the Python dev version (`cv_engine/SeeSharpPy`):
`pipeline/` (IFrameSource, CameraSource/ShmSource, 3-thread Pipeline with
bounded queues and injected sink), `processing/` (factory + processors),
`model/`, `config/`. `src/main.cpp` is the composition root.

## Features
- Blob / line / circle / ArUco detection
- NN classifier (ONNX via cv::dnn on desktop, RKNN on the module NPU)
- Configurable processing pipeline via `config.json` (`processing_mode`)
- Real-time three-thread pipeline with frame-drop backpressure

## Blob color editor (web UI)
In **Цветовые шаблоны / One color patterns**, select HSL, HSV, RGB, CMYK,
HSLuv or YCbCr. HSL, HSV and HSLuv offer a hue/saturation wheel. All models
have a palette, sliders and numeric color controls, followed by **Minimum /
Maximum** fields for each channel. These bounds are used directly by the
C++ detector in the selected model. Picking a target color shifts the intervals
while preserving their widths. For hue, minimum greater than maximum wraps
through 0° (for example, 350°–10° selects reds).

`color_model` and `lower_range` / `upper_range` are saved per pattern:

- RGB: R, G, B, each 0–255.
- YCbCr: Y, Cb, Cr, each 0–255.
- HSL / HSLuv: H 0–360°, S and L 0–100%.
- HSV: H 0–360°, S and V 0–100%.
- CMYK: C, M, Y, K, each 0–100%; approximate sRGB conversion without a print profile.

Switching models computes an approximate initial interval: a rectangular range
cannot generally be converted exactly between color spaces. Review the new
bounds before Apply/Save. Sampling a contour on the video also uses the selected
model. Configurations without `color_model` retain legacy Y, Cr, Cb semantics;
the web editor migrates them to YCbCr channel order. Obsolete
`min/max_luminance` and `min/max_chrominance_*` fields are removed by the editor.

The editor works offline. HSLuv 1.0.1 is embedded from
[hsluv-javascript](https://github.com/hsluv/hsluv-javascript/tree/v1.0.1), with
its MIT license in `include/web/vendor/hsluv/LICENSE`. The native HSLuv math is
adapted from the same implementation. Converted frames are shared by patterns
using the same model; HSLuv costs more CPU than simpler models.

Run `node tests/blob_color_picker.test.cjs` from this directory (Node.js and g++
required). This checks conversions, legacy migration and native/editor parity.
Append an output HTML path to generate a standalone browser interaction test;
a successful test sets `body[data-test="PASS"]`.
`node tests/param_meta.test.cjs` checks the parameter metadata of the web form
(`include/web/param_meta_js.hpp`): dropdown options, numeric steps, the
`fieldRow` renderer and that every English label/hint has a Russian
counterpart. The native mask test also requires OpenCV core:

```bash
g++ -std=c++17 -I include -I /usr/include/opencv4 tests/blob_color_mask.test.cpp -lopencv_core -o /tmp/blob-color-mask-test
/tmp/blob-color-mask-test
```

## UART protocols and developer self-tests
`include/transport/binary_uart_protocol.hpp` and `dxl_packet_parser.hpp` hold
the pure framing code; `include/diagnostics/uart_unit_tests.hpp` checks it.
The same checks, plus live checks of the running program and an optional
TX-RX loopback, run on the device from the web UI **Developer mode**
(`src/diagnostics/self_test.cpp`, `GET /dev/tests`, `POST /dev/tests/run`).
Host test, including the binary and DXL transports on a pty pair:

```bash
g++ -std=c++17 -I include -I third_party -I /usr/include/opencv4 tests/uart_protocol.test.cpp \
    src/transport/binary_uart_transport.cpp src/transport/dxl_uart_transport.cpp \
    src/platform/serial.cpp -lopencv_core -pthread -lutil -o /tmp/uart-protocol-test
/tmp/uart-protocol-test
```

`sh board/host/run_tests.sh` from the repository root runs this test, the blob
mask test and all `tests/*.test.cjs` in one go.

## Platform layer
`include/platform/` hides the OS differences behind one API (POSIX branch is
the one built and tested; the `_WIN32` branches are kept for a possible native
Windows build but are not compiled — on Windows the host UI runs in WSL):

- `socket.hpp` — BSD sockets vs WinSock2 (`socket_t`, close, timeouts,
  non-blocking, poll) for the HTTP server, UDP and WebSocket transports;
- `serial.hpp` — `SerialPort`: termios vs Win32 COM port (binary UART,
  USB stream, self-test loopback);
- `process.hpp` — `ChildProcess`: fork/execve vs CreateProcess + Job Object
  (python trainer).

Linux-only diagnostics (`/proc` port users, kernel console check) would report
SKIP on Windows.

Run `node tests/dev_mode_ui.test.cjs` to check the UART log and developer-mode
JS helpers.

## Training tab (web UI)
The **Training** tab (`include/web/training_js.hpp`, served by
`src/training/training_service.cpp`) collects classifier samples from the live
stream (whole frame, a rectangle drawn on the canvas, or blob detector boxes),
runs `common/nn/simple_classifier.py` as a low-priority subprocess, lists and
activates ONNX models, exports datasets as zip and accepts model uploads. It
also labels frames for YOLO (trained on a host PC). `ClassifierProcessor`
supports `region_mode` = `whole | roi | blob`; `include/processing/region_crop.hpp`
is the single crop function shared by inference and dataset capture.
Run `node tests/training_ui.test.cjs` to check the pure JS helpers.

## Dependencies
- C++17, CMake >= 3.10
- OpenCV (desktop >= 4.x; module: buildroot opencv3 3.3 + contrib/aruco)
- nlohmann_json (system package or vendored `third_party/nlohmann/json.hpp`)
- Module only: librknn_api (buildroot rknpu)

## Build (desktop)
```bash
mkdir build && cd build
cmake ..
make
```

## Build (MTV3 module, RV1126)
See `sw/board/mtv3-rv1126/README.md` for the full bring-up. In short:
```bash
mkdir build-rv1126 && cd build-rv1126
cmake -DCMAKE_TOOLCHAIN_FILE=../../../board/mtv3-rv1126/toolchain-rv1126.cmake \
      -DMTV3_BOARD=ON ..
make -j
```

## Build (Raspberry Pi CM5)
See `../../board/raspberry-cm5/README.md` for prerequisites, camera setup and
service installation. Build directly on Raspberry Pi OS:
```bash
mkdir build-cm5 && cd build-cm5
cmake -DRASPBERRY_CM5=ON ..
cmake --build . -j"$(nproc)"
```
CM5 uses `model_onnx` from `config.json`; `model_rknn` is ignored. MTV3 uses
`model_rknn` for both frame classification and YOLO object detection. The RKNN
YOLO model must expose one decoded prediction tensor; its orientation and
objectness field are described by `output_layout`, `output_attributes`, and
`output_has_objectness`.

## Getting Started
1. Edit `config.json` (`general_params.processing_mode`) to select the module.
2. Run `mainCV`. Desktop: shows original|processed side by side, ESC to quit.
   MTV3: reads `/dev/shm/mtv3cam` (start `mtv3_cam_daemon --shm` first).
   CM5: reads a V4L2 camera (`--camera N`, default `0`). Both board targets
   run headlessly, print fps every 100 frames and expose control UI on `:8081`.

## Contributing
Pull requests are welcome. Please follow the [naming_conventions.md](./naming_conventions.md) guidelines.
