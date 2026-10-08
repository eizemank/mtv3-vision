# Host web-interface test version

This target runs the real SeeSharp processing manager and embedded HTTP UI on
a development computer without a camera or CM5: `SyntheticSource` produces two
`DICT_4X4_50` ArUco markers, configured color blobs and CC0 photographic
person/car test scenes at about 30 FPS. CMake copies the photographs to the
build directory; without them the source falls back to programmatic
silhouettes.

Supported hosts:

| Host | Launcher | Toolchain |
|---|---|---|
| Ubuntu/Debian (native) | `sh board/host/run_web_ui.sh` | GCC/Clang, CMake, OpenCV from apt |
| Windows 10/11 | `.\board\host\run_web_ui.ps1` | Ubuntu inside WSL, same build as native Ubuntu |

Native Windows builds are not supported: CMake stops with a message pointing
to the WSL launcher.

## Ubuntu / Debian (native or WSL)

Install the dependencies once (compiler, CMake, OpenCV with aruco/dnn,
nlohmann-json, Python, curl; `--with-tests` adds Node.js for the UI tests):

```bash
sh board/host/setup_ubuntu.sh            # or: --with-tests
```

Build and run from the repository root:

```bash
sh board/host/run_web_ui.sh
```

Options:

| Option / variable | Effect |
|---|---|
| `--download-yolo` | fetch `yolo11n.onnx` + `coco.names` into the build directory and switch the config to `object_detection` |
| `--build-only` | configure and build without starting `mainCV` (CI, tests) |
| `--debug` | `CMAKE_BUILD_TYPE=Debug` (also `SEESHARP_BUILD_TYPE=...`) |
| `SEESHARP_HOST_BUILD` | build directory, default `~/.cache/seesharp/build-host-ui` |
| `SEESHARP_BUILD_JOBS` | parallel compile jobs, default 4 |

The launcher rewrites `config.json` in the build directory for the host
profile (circle detection, debug mode, all transports off) and starts
`./mainCV --config ./config.json` from there, so relative model paths resolve
the same way as on CM5. Open `http://127.0.0.1:8081/`.

If CMake previously reported `No CMAKE_CXX_COMPILER could be found`, install
`build-essential` and rerun: the launcher removes the stale failed cache
automatically. Run the launcher as your normal user, never through `sudo`; if
an earlier run created root-owned files, restore ownership once:

```bash
sudo chown -R "$USER:$USER" "$HOME/.cache/seesharp"
```

### Tests

```bash
sh board/host/run_tests.sh
```

runs the web UI unit tests (`cpp/SeeSharp/tests/*.test.cjs`, needs Node.js),
the blob color mask parity test and the UART protocol/transport test on a pty
pair (both need `g++` and OpenCV core). When `pkg-config opencv4` points at a
different OpenCV than the CMake build (two installations on one machine),
pass the flags explicitly:

```bash
SEESHARP_OPENCV_CFLAGS="" SEESHARP_OPENCV_LIBS="-L/usr/local/lib -lopencv_core" \
    sh board/host/run_tests.sh
```

## Windows (WSL)

Install WSL with Ubuntu once (elevated PowerShell), then the Ubuntu
dependencies inside it:

```powershell
wsl --install -d Ubuntu
```

```bash
sh board/host/setup_ubuntu.sh      # inside the Ubuntu shell, repository root
```

Build and run from Windows PowerShell in the repository root:

```powershell
.\board\host\run_web_ui.ps1
.\board\host\run_web_ui.ps1 -DownloadYolo   # once, to enable YOLO
.\board\host\run_web_ui.ps1 -BuildOnly
```

The launcher picks an installed distribution (Ubuntu preferred), converts the
repository path with `wslpath` and runs `board/host/run_web_ui.sh` inside
WSL; the build lives in the Linux filesystem under `~/.cache/seesharp`, which
avoids DrvFS ACL problems and is faster than `/mnt/c`. Windows browsers open
the WSL service directly through `http://127.0.0.1:8081/`.

If PowerShell reports `HCS_E_SERVICE_NOT_AVAILABLE`, WSL2 cannot create its
virtual machine. Open PowerShell as Administrator and run:

```powershell
dism.exe /online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart
dism.exe /online /enable-feature /featurename:VirtualMachinePlatform /all /norestart
wsl --install -d Ubuntu
bcdedit /set hypervisorlaunchtype auto
```

Enable Intel VT-x/AMD-V in BIOS/UEFI and reboot. Verify with `wsl -e /bin/true`.
When hardware virtualization cannot be enabled, WSL1 is sufficient for this
host UI build:

```powershell
wsl --set-default-version 1
wsl --set-version Ubuntu 1  # convert an existing Ubuntu distribution
wsl --install -d Ubuntu
```

After Ubuntu starts, run `sh board/host/setup_ubuntu.sh` inside it and start
the PowerShell launcher again.

If the error is `Wsl/Service/ERROR_PATH_NOT_FOUND`, inspect installed
distributions first:

```powershell
wsl --shutdown
wsl --update
wsl --list --verbose
```

The launcher selects an installed distribution explicitly instead of
assuming that the default one is valid. If a listed distribution has lost its
virtual disk or installation directory, it must be restored from backup or
reinstalled. The following commands permanently delete that distribution and
all files stored inside it, so use them only when its data is disposable:

```powershell
wsl --unregister Ubuntu
wsl --install -d Ubuntu
```

## Using the host UI

The right column shows the synthetic source and the active detector output
and stays on screen while the parameters scroll; **Apply**, **Save** and
**Revert** are pinned at the bottom. Change algorithm parameters, select
another mode, and use **Apply** to test hot reload. Modes requiring an
external model, such as `object_detection`, need their model file in the build
directory. The operator guide is `docs/DETECTOR_UI_GUIDE_RU.md`.

In `blob_detection`, hold the pointer and trace a closed contour around a
representative blob in the **Source** pane, select its pattern index, and click
**Configure blob from selected contour**. The UI samples only pixels inside the
polygon and estimates color limits plus initial area, width, and height
constraints. Review the generated values and click **Apply** or **Save**; trace
close to the object boundary for the best color estimate.

To enable YOLO object detection in the host UI, launch once with
`--download-yolo` (Linux) or `-DownloadYolo` (Windows/WSL). The launcher
reconfigures CMake before invoking the download target, avoiding stale build
files without `download_yolo11n`. Then select `object_detection`.

The synthetic host profile excludes hardware transports and system
administration. Use the camera-debug profile below for transport integration.

## Real camera and transport debugging

The camera-debug profile uses the host camera, publishes full JSON metadata via
WebSocket over Ethernet, and sends lightweight binary metadata through a USB-UART
adapter. Run it on Linux or WSL with the camera and serial device passed through:

```bash
sudo usermod -aG video,dialout "$USER"
# Log out and back in after changing groups.
sh board/host/run_camera_debug.sh \
  --camera 0 --uart /dev/ttyUSB0 --ws-port 5002
```

The launcher reuses `yolo11n.onnx` and `coco.names` from the synthetic host
build or `common/models` when available. To download them directly into the
camera-debug build, run once with `--download-yolo`:

```bash
sh board/host/run_camera_debug.sh --camera 0 --uart /dev/ttyUSB0 \
  --ws-port 5002 --download-yolo
```

For WSL/USBIP the launcher requests MJPEG 640x480 at 15 FPS by default. This
reduces USB bandwidth and prevents partially transferred frames with a green
lower area. Override it when the camera supports another stable mode:

```bash
sh board/host/run_camera_debug.sh --camera 0 --uart /dev/ttyUSB0 \
  --width 320 --height 240 --fps 10
```

Use `--no-mjpeg` only if the camera does not advertise an MJPEG capture mode.

Open the UI at `http://<host-ip>:8081/`. Receive metadata from another Ethernet
computer:

```bash
python3 -m pip install websocket-client
python3 board/host/websocket_metadata_receiver.py \
  ws://<host-ip>:5002/metadata
```

The USB-UART adapter must use TTL levels compatible with the receiver. Connect
adapter TX to receiver RX and connect GND; SeeSharp only transmits on this port.
The default format is the lightweight binary UART protocol documented in
`board/raspberry-cm5/TRANSPORTS.md`.

To verify the UART stream with a second USB-UART adapter on the receiving host:

```bash
python3 -m pip install pyserial
python3 board/host/uart_metadata_receiver.py /dev/ttyUSB1
```

### Camera and USB-UART in WSL2

Windows devices are not automatically visible as `/dev/video0` or
`/dev/ttyUSB0` inside WSL2. Install `usbipd-win`, keep a WSL terminal open, and
list devices from PowerShell:

```powershell
winget install --interactive --exact dorssel.usbipd-win
usbipd list
```

Share each required device once from an elevated PowerShell and then attach it
from a regular PowerShell. Repeat for the webcam and USB-UART adapter using their
own BUSID values:

```powershell
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

Verify inside WSL:

```bash
sudo modprobe uvcvideo
lsusb
ls -l /dev/video* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
v4l2-ctl --list-devices
```

Install `v4l2-ctl` with `sudo apt install v4l-utils`. While attached to WSL, a
USB device cannot be used by Windows. Detach it when finished:

```powershell
usbipd detach --busid <BUSID>
```

Some integrated webcams use USB isochronous transfers or require a UVC driver
not present in the installed WSL kernel. If the camera appears in `lsusb` but no
`/dev/videoN` is created, update WSL with `wsl --update`; otherwise use an
external UVC camera, a custom WSL kernel with `CONFIG_USB_VIDEO_CLASS`, or run
the camera-debug build on native Linux.

Manual build equivalent:

```bash
cmake -S cpp/SeeSharp -B "$HOME/.cache/seesharp/build-host-ui" \
    -DHOST_WEB_UI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$HOME/.cache/seesharp/build-host-ui" -j4
cd "$HOME/.cache/seesharp/build-host-ui"
./mainCV --config ./config.json
```
