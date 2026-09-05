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
