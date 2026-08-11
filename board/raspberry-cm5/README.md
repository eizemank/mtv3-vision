# Raspberry Pi Compute Module 5 — SeeSharp

CM5 runs the same SeeSharp processing modes as MTV3: blob, line, circle,
ArUco and classification. It uses a local V4L2 camera and OpenCV `cv::dnn`
with an ONNX model; RKNN and the MTV3 shared-memory camera daemon are not
used.

## Prerequisites

Use 64-bit Raspberry Pi OS Bookworm on CM5 with the camera connected and
enabled. Install the runtime and build dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev \
    nlohmann-json3-dev libcamera-tools libcamera-v4l2
```

Check the camera before building:

```bash
rpicam-hello --list-cameras
v4l2-ctl --list-devices
```

For a CSI camera, OpenCV needs libcamera's V4L2 compatibility layer. Run the
application through `libcamerify`. A USB UVC camera normally works directly.
If `v4l2-ctl` is unavailable, install `v4l-utils`.

## Build

Run on the CM5 in the repository checkout:

```bash
cd ~/mtv3-vision/cpp/SeeSharp
mkdir -p build-cm5
cd build-cm5
cmake -DRASPBERRY_CM5=ON ..
cmake --build . -j"$(nproc)"
```

The build copies `common/config/config.json` into `build-cm5/config.json`.
For `classification`, copy `simple_classifier.onnx` beside `mainCV` and set
`general_params.processing_mode` to `classification`. The CM5 ignores
`classification.model_rknn`.

## Manual start

CSI camera:

```bash
cd ~/mtv3-vision/cpp/SeeSharp/build-cm5
libcamerify ./mainCV --camera 0 --config ./config.json --dump 10 --dump-dir /tmp/seesharp
```

USB V4L2 camera:

```bash
./mainCV --camera 0 --config ./config.json --dump 10 --dump-dir /tmp/seesharp
```

`--camera N` selects `/dev/videoN`. The program is headless, writes the last
processed and source frames to `--dump-dir` when `--dump` is set, prints FPS
and detection metadata to stdout, and provides a live preview and configuration
UI at `http://<cm5-ip>:8081/` (or `http://localhost:8081/` in the CM5 browser).
No separate HTTP server is required.

## Autostart

Install the binary, config and optional ONNX model, then enable the supplied
systemd unit:

```bash
sudo install -d /opt/seesharp /tmp/seesharp
sudo install -m 0755 build-cm5/mainCV /opt/seesharp/mainCV
sudo install -m 0644 build-cm5/config.json /opt/seesharp/config.json
sudo install -m 0644 ~/mtv3-vision/board/raspberry-cm5/systemd/seesharp-cm5.service /etc/systemd/system/seesharp-cm5.service
sudo systemctl daemon-reload
sudo systemctl enable --now seesharp-cm5
journalctl -u seesharp-cm5 -f
```

For a CSI camera, keep `ExecStart=/usr/bin/libcamerify ...` in the unit. For a
USB camera replace it with `/opt/seesharp/mainCV ...`. Adjust `--camera`
after checking the V4L2 device list.
