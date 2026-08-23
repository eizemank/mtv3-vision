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

In `blob_detection`, drag a rectangle around a representative blob in the
**Source** pane, select its pattern index, and click **Configure blob from
selected area**. The UI estimates robust YCrCb limits and initial area, width,
and height constraints from the selected pixels. Review the generated values
and click **Apply** or **Save**; use a tightly cropped region with little
background for the best color estimate.

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

The host target intentionally excludes CM5 UART/UDP/USB transports and system
administration. It is intended for UI and algorithm-configuration testing, not
hardware integration testing.

Manual build equivalent:

```bash
cmake -S cpp/SeeSharp -B "$HOME/.cache/seesharp/build-host-ui" \
    -DHOST_WEB_UI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$HOME/.cache/seesharp/build-host-ui" -j4
cd "$HOME/.cache/seesharp/build-host-ui"
./mainCV --config ./config.json
```
