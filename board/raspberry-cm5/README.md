# Raspberry Pi Compute Module 5 — SeeSharp

CM5 runs the same SeeSharp processing modes as MTV3: blob, line, circle,
ArUco and YOLO object detection. It uses a local V4L2 camera and OpenCV `cv::dnn`
with an ONNX model; RKNN and the MTV3 shared-memory camera daemon are not
used.

## Prerequisites

Use 64-bit Raspberry Pi OS Bookworm on CM5 with the camera connected and
enabled. Install the runtime and build dependencies:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev \
    nlohmann-json3-dev libcamera-tools libcamera-v4l2 python3-venv
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
To download the pretrained YOLO11n COCO model and export it to ONNX, run:

```bash
cmake --build . --target download_yolo11n
```

The target creates a local Python venv for export, installs Ultralytics, writes
`yolo11n.onnx` and COCO labels, and selects `object_detection`. The detector
returns independent bounding boxes and applies confidence filtering, NMS and
the configured `max_objects` limit (10 by default).

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

The page displays the source camera frame and the annotated detector result at
the same time. Algorithm parameters can be edited and applied without leaving
the page; the result preview immediately reflects the active configuration.
The same HTTP interface is available over Wi-Fi/Ethernet and over USB.

## System administration UI

The web interface includes a CM5 administration panel for CPU temperature and
load, memory usage, process listing and signalling, NetworkManager connections,
static/DHCP IPv4 configuration, Wi-Fi client/AP switching, restricted file
access and an optional system terminal.

Before using it, set a strong random `system_admin.token` in `config.json` and
enter the same value in the web panel. An empty token disables every `/admin`
endpoint. File operations are confined to `system_admin.file_root` and files
larger than 1 MiB are rejected. Terminal execution is disabled by default; set
`system_admin.terminal_enabled` to `true` only on an isolated trusted network.
The terminal runs commands with the privileges of `mainCV`, has a 10-second
timeout and is therefore equivalent to remote shell access.

Network changes use `nmcli`. Install NetworkManager if it is not present:

```bash
sudo apt install -y network-manager
```

Supported network request modes are `dhcp`, `static`, `client` and `ap`. A
request supplies the NetworkManager connection name and the fields needed by
the selected mode. Applying a network change can immediately disconnect the
current browser session.

Camera orientation is configured in `general_params.camera_rotation`. Allowed
clockwise values are `0`, `90`, `180` and `270`; changes are applied by config
hot reload before detection, HTTP preview and UDP video output.

## Image and detector tuning

`general_params` also supports software image correction before every
detector: `exposure_ev` (one unit doubles/halves brightness),
`white_balance_bgr` (three channel gains), `contrast` and `brightness`.
These controls are independent of libcamera automatic exposure/white balance;
for sensor-level tuning pass the corresponding controls through the camera
stack when launching `libcamerify`.

Blob patterns already configure Y/Cr/Cb ranges (`lower_range`/`upper_range`),
area, width/height, circularity, inertia and convexity. Optional polygon fields
add shape filtering:

```json
"min_vertices": 4,
"max_vertices": 4,
"polygon_approximation": 0.02
```

Use 3 vertices for triangles and 4 for quadrilaterals. Their physical image
size is limited with `min_area`, `max_area`, `min_width` and `min_height`.
Relative position, distance and angle of several colored regions are configured
by `multicolor_patterns.nodes` and `multicolor_patterns.links`. One composite
object supports up to five primitives; the default pattern demonstrates all five.
Up to five composite-object instances are returned per frame; configure this
with `blob_detection.max_composite_objects`.

`aruco_detection.dictionary` selects marker shape/code family, `allowed_ids`
filters encoded values, and `min_area`/`max_area` filter marker image size.
Circle size is controlled by `circle_detection.min_radius`/`max_radius`.
Line detection supports length/gap, Canny contrast thresholds, angle range,
normalized ROI (`roi_x`, `roi_y`, `roi_width`, `roi_height`) and `max_lines`.

Neural-network training is deliberately offline. Configure `training.data_yaml`,
`base_model`, `epochs`, `image_size` and `output_onnx`, install `ultralytics`,
then run:

```bash
python3 board/raspberry-cm5/train_yolo.py build-cm5/config.json
```

The dataset YAML and labels use the standard Ultralytics detection format.
Restart or hot-reload `object_detection` after export.

Metadata output over UDP and the Dynamixel 1.0 UART virtual device are
documented in [`TRANSPORTS.md`](./TRANSPORTS.md).
The same transport module can send annotated JPEG video in a separate UDP
worker; enable `transports.udp_video` and run `udp_video_receiver.py` on the
destination computer.
For a direct USB cable, `setup_usb_gadget.sh` creates a CDC ACM device and
`transports.usb_stream` multiplexes JSON metadata and JPEG video over it.
It also creates a USB Ethernet interface. Install and reboot once:

```bash
sudo sh board/raspberry-cm5/setup_usb_gadget.sh
sudo reboot
```

The CM5 uses `192.168.7.2/24` on `usb0`. Configure the host-side USB network
interface as `192.168.7.1/24`, then open `http://192.168.7.2:8081/`. CDC ACM
streaming through `/dev/ttyGS0` remains available simultaneously. Do not feed
5 V into the CM5 from two independent supplies unless the carrier board is
designed for it.

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
sudo install -m 0644 build-cm5/yolo11n.onnx /opt/seesharp/yolo11n.onnx
sudo install -m 0644 build-cm5/coco.names /opt/seesharp/coco.names
sudo install -m 0644 ~/mtv3-vision/board/raspberry-cm5/systemd/seesharp-cm5.service /etc/systemd/system/seesharp-cm5.service
sudo install -m 0755 ~/mtv3-vision/board/raspberry-cm5/check_boot_sla.sh /usr/local/bin/check-seesharp-boot
sudo systemctl daemon-reload
sudo systemctl disable seesharp-cm5 2>/dev/null || true
sudo systemctl enable --now seesharp-cm5
journalctl -u seesharp-cm5 -f
```

### 12-second boot requirement

The service starts in the `sysinit.target` phase, does not wait for networking
or the USB gadget, and runs with elevated CPU/I/O priority. Periodic JPEG dumps
are disabled in the production unit. UART `startup_push` is enabled by default,
so the first completed detection frame is transmitted without waiting for a
controller request. On a shared Dynamixel bus, disable startup push unless the
master reserves an idle transmission window.

After every cold boot, check the software timing marker:

```bash
sudo check-seesharp-boot 12000
sudo journalctl -b -u seesharp-cm5.service | grep BOOT_SLA
```

The marker uses Linux `CLOCK_BOOTTIME` and therefore measures kernel start to
the first successfully written detection packet. The formal power-on test must
also include boot-ROM/firmware time: connect a logic analyser to the power-enable
signal and UART TX, repeat at least 20 cold starts, and verify the worst result
is no more than 12 seconds. Use Raspberry Pi OS Lite, keep the default ArUco or
another fast startup detector, and avoid loading YOLO at boot when the measured
model initialization leaves insufficient margin.

For a CSI camera, keep `ExecStart=/usr/bin/libcamerify ...` in the unit. For a
USB camera replace it with `/opt/seesharp/mainCV ...`. Adjust `--camera`
after checking the V4L2 device list.
