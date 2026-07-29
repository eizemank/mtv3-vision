# SeeSharp — Computer Vision Engine (C++)

## Overview
Modular C++ computer vision application: OpenCV processing pipeline with a
JSON-configurable set of recognition algorithms. Runs on desktop (camera or
video file, GUI) and on the MTV3 module (RV1126: frames from the camera
daemon's shared-memory ring, headless, NN inference on the NPU via RKNN).

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

## Getting Started
1. Edit `config.json` (`general_params.processing_mode`) to select the module.
2. Run `mainCV`. Desktop: shows original|processed side by side, ESC to quit.
   Module: reads `/dev/shm/mtv3cam` (start `mtv3_cam_daemon --shm` first),
   prints fps every 100 frames.

## Contributing
Pull requests are welcome. Please follow the [naming_conventions.md](./naming_conventions.md) guidelines.
