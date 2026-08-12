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
To install the Apache-2.0 MobileNetV2 model from ONNX Model Zoo, its 1000
ImageNet labels and the matching preprocessing configuration, run:

```bash
cmake --build . --target download_mobilenetv2
```

The script verifies the model SHA-256 and updates only the `classification`
section. Select `classification` in the web UI or set
`general_params.processing_mode` manually. The model classifies the dominant
object in the entire frame; it is not an object detector and does not produce
separate bounding boxes. The CM5 ignores `classification.model_rknn`.

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

Camera orientation is configured in `general_params.camera_rotation`. Allowed
clockwise values are `0`, `90`, `180` and `270`; changes are applied by config
hot reload before detection, HTTP preview and UDP video output.

Metadata output over UDP and the Dynamixel 1.0 UART virtual device are
documented in [`TRANSPORTS.md`](./TRANSPORTS.md).
The same transport module can send annotated JPEG video in a separate UDP
worker; enable `transports.udp_video` and run `udp_video_receiver.py` on the
destination computer.

To enable the real 3.3 V UART on GPIO14/GPIO15 and remove the serial console
from it, run `sudo sh board/raspberry-cm5/setup_uart.sh`, reboot, and use
`/dev/serial0`. UART metadata output is enabled in the CM5 configuration by
default.

When opening the UI from another computer, `localhost` refers to that computer,
not the CM5; use the CM5 IP address. Verify the local server with:

```bash
curl http://127.0.0.1:8081/config
```

If libcamera reports `Camera frontend has timed out`, reproduce it without
SeeSharp using `rpicam-vid -t 0`. The timeout comes from the CSI camera path:
power down the CM5, reseat both ends of the FFC cable with the contacts in the
correct orientation, verify the CAM0/CAM1 connector and try another cable or
sensor. SeeSharp automatically reopens the V4L2 stream after a read timeout.

## Autostart

Install the binary, config and optional ONNX model, then enable the supplied
systemd unit:

```bash
sudo install -d /opt/seesharp /tmp/seesharp
sudo install -m 0755 build-cm5/mainCV /opt/seesharp/mainCV
sudo install -m 0644 build-cm5/config.json /opt/seesharp/config.json
sudo install -m 0644 build-cm5/mobilenetv2-12.onnx /opt/seesharp/mobilenetv2-12.onnx
sudo install -m 0644 build-cm5/imagenet_classes.txt /opt/seesharp/imagenet_classes.txt
sudo install -m 0644 ~/mtv3-vision/board/raspberry-cm5/systemd/seesharp-cm5.service /etc/systemd/system/seesharp-cm5.service
sudo systemctl daemon-reload
sudo systemctl enable --now seesharp-cm5
journalctl -u seesharp-cm5 -f
```

For a CSI camera, keep `ExecStart=/usr/bin/libcamerify ...` in the unit. For a
USB camera replace it with `/opt/seesharp/mainCV ...`. Adjust `--camera`
after checking the V4L2 device list.
