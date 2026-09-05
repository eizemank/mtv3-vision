# Host web-interface test version

This target runs the real SeeSharp processing manager and embedded HTTP UI on
a Linux development computer or in WSL. It does not require a camera or CM5:
`SyntheticSource` produces two `DICT_4X4_50` ArUco markers, configured YCrCb
color blobs, and CC0 photographic person/car test scenes at about 30 FPS. CMake
copies the photographs to the host build directory. If those assets are absent,
the source falls back to the original programmatic silhouettes.

## Dependencies

Ubuntu/Debian or WSL:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev \
    nlohmann-json3-dev python3
```

If CMake previously reported `No CMAKE_CXX_COMPILER could be found`, install
`build-essential` using the command above and rerun the launcher. It detects
and removes the stale failed CMake cache automatically.

## Run

From the repository root:

```bash
sh board/host/run_web_ui.sh
```

From Windows PowerShell with WSL installed:

```powershell
.\board\host\run_web_ui.ps1
```

Windows browsers can normally open the WSL service directly through
`http://127.0.0.1:8081/`.

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

After Ubuntu starts, install the dependencies from the section above and run
the PowerShell launcher again.

If the error is `Wsl/Service/ERROR_PATH_NOT_FOUND`, inspect installed
distributions first:

```powershell
wsl --shutdown
wsl --update
wsl --list --verbose
```

The launcher now selects an installed distribution explicitly instead of
assuming that the default one is valid. If a listed distribution has lost its
virtual disk or installation directory, it must be restored from backup or
reinstalled. The following commands permanently delete that distribution and
all files stored inside it, so use them only when its data is disposable:

```powershell
wsl --unregister Ubuntu
wsl --install -d Ubuntu
```

Open `http://127.0.0.1:8081/`. The left pane shows the synthetic source and the
right pane shows the active detector output. Change algorithm parameters,
select another mode, and use **Apply** to test hot reload. Modes requiring an
external model, such as `object_detection`, need their model file in the build
directory.

In `blob_detection`, hold the pointer and trace a closed contour around a
representative blob in the **Source** pane, select its pattern index, and click
**Configure blob from selected contour**. The UI samples only pixels inside the
polygon and estimates robust YCrCb limits plus initial area, width, and height
constraints. Review the generated values and click **Apply** or **Save**; trace
close to the object boundary for the best color estimate.

To enable YOLO object detection in the host UI, install the download prerequisites
and launch once with `--download-yolo`:

```bash
sudo apt install -y curl python3
sh board/host/run_web_ui.sh --download-yolo
```

Run the launcher as your normal user, not through `sudo`. If an earlier run
created root-owned build files, restore ownership once:

```bash
sudo chown -R "$USER:$USER" "$HOME/.cache/seesharp"
```

The launcher stores build artifacts under `~/.cache/seesharp/build-host-ui`,
not under the repository on `/mnt/c`. WSL's Linux filesystem avoids Windows
ACL failures such as `configure_file: Operation not permitted` and builds
faster than DrvFS.

PowerShell equivalent:

```powershell
.\board\host\run_web_ui.ps1 -DownloadYolo
```

The launcher reconfigures CMake before invoking the download target, avoiding
stale Makefiles without `download_yolo11n`. Then select `object_detection`.
The launcher runs
`mainCV` from the build directory, so relative `yolo11n.onnx` and `coco.names`
paths resolve consistently with the CM5 deployment.

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
