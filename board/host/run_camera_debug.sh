#!/bin/sh
set -eu

if [ "$(id -u)" -eq 0 ]; then
    echo "Do not run the host camera debugger with sudo." >&2
    exit 1
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE="$ROOT/cpp/SeeSharp"
BUILD=${SEESHARP_CAMERA_BUILD:-$HOME/.cache/seesharp/build-host-camera}
CAMERA=0
UART=${SEESHARP_UART_DEVICE:-/dev/ttyUSB0}
WS_PORT=${SEESHARP_WS_PORT:-5002}
CAMERA_WIDTH=${SEESHARP_CAMERA_WIDTH:-640}
CAMERA_HEIGHT=${SEESHARP_CAMERA_HEIGHT:-480}
CAMERA_FPS=${SEESHARP_CAMERA_FPS:-15}
CAMERA_MJPEG=true
DOWNLOAD_YOLO=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        --camera) CAMERA=$2; shift 2 ;;
        --uart) UART=$2; shift 2 ;;
        --ws-port) WS_PORT=$2; shift 2 ;;
        --width) CAMERA_WIDTH=$2; shift 2 ;;
        --height) CAMERA_HEIGHT=$2; shift 2 ;;
        --fps) CAMERA_FPS=$2; shift 2 ;;
        --no-mjpeg) CAMERA_MJPEG=false; shift ;;
        --download-yolo) DOWNLOAD_YOLO=true; shift ;;
        *) echo "Usage: $0 [--camera N] [--uart DEVICE] [--ws-port PORT] [--width PX] [--height PX] [--fps N] [--no-mjpeg] [--download-yolo]" >&2; exit 2 ;;
    esac
done

CAMERA_DEVICE="/dev/video$CAMERA"
if [ ! -e "$CAMERA_DEVICE" ]; then
    echo "Host camera is not available in Linux: $CAMERA_DEVICE" >&2
    echo "Available V4L2 devices:" >&2
    ls -l /dev/video* 2>/dev/null >&2 || echo "  none" >&2
    echo "On WSL2 attach the USB camera with usbipd-win, then load uvcvideo." >&2
    echo "See board/host/README.md, section 'Camera and USB-UART in WSL2'." >&2
    exit 1
fi
if [ ! -e "$UART" ]; then
    echo "Warning: USB-UART is not available: $UART" >&2
    echo "WebSocket and web UI will work, but UART metadata will be disabled." >&2
fi

for command in cmake c++ pkg-config python3; do
    command -v "$command" >/dev/null 2>&1 || {
        echo "Missing command: $command" >&2
        echo "Install: sudo apt install build-essential cmake pkg-config libopencv-dev nlohmann-json3-dev python3" >&2
        exit 1
    }
done

cmake -S "$SOURCE" -B "$BUILD" \
    -DHOST_WEB_UI=ON -DHOST_CAMERA_SOURCE=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel "${SEESHARP_BUILD_JOBS:-4}"

copy_yolo_asset() {
    asset=$1
    [ -s "$BUILD/$asset" ] && return 0
    for source_dir in \
        "$HOME/.cache/seesharp/build-host-ui" \
        "$ROOT/cpp/SeeSharp/build-host-ui" \
        "$ROOT/common/models"
    do
        if [ -s "$source_dir/$asset" ]; then
            cp "$source_dir/$asset" "$BUILD/$asset"
            echo "Copied YOLO asset: $source_dir/$asset"
            return 0
        fi
    done
    return 1
}

copy_yolo_asset yolo11n.onnx || true
copy_yolo_asset coco.names || true
if [ "$DOWNLOAD_YOLO" = true ]; then
    cmake --build "$BUILD" --target download_yolo11n
fi
if [ ! -s "$BUILD/yolo11n.onnx" ] || [ ! -s "$BUILD/coco.names" ]; then
    echo "Warning: YOLO assets are incomplete in $BUILD." >&2
    echo "Run this launcher once with --download-yolo before selecting YOLO." >&2
fi

python3 - "$BUILD/config.json" "$UART" "$WS_PORT" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
config = json.loads(path.read_text())
transports = config.setdefault("transports", {})
transports["websocket_metadata"] = {
    "enabled": True,
    "bind": "0.0.0.0",
    "port": int(sys.argv[3]),
}
transports["uart_binary"] = {
    "enabled": True,
    "device": sys.argv[2],
    "baud": 115200,
    "max_objects": 20,
}
for name in ("udp_metadata", "udp_video", "usb_stream", "uart_dxl"):
    if name in transports:
        transports[name]["enabled"] = False
config["general_params"]["debug_mode"] = True
path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
PY

HOST_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
echo "Host camera: $CAMERA"
echo "Capture: ${CAMERA_WIDTH}x${CAMERA_HEIGHT} @ ${CAMERA_FPS} FPS, MJPEG=$CAMERA_MJPEG"
echo "Web UI: http://${HOST_IP:-127.0.0.1}:8081/"
echo "Metadata: ws://${HOST_IP:-127.0.0.1}:$WS_PORT/metadata"
echo "Binary UART: $UART @ 115200"
cd "$BUILD"
set -- ./mainCV --config ./config.json --camera "$CAMERA" \
    --camera-width "$CAMERA_WIDTH" --camera-height "$CAMERA_HEIGHT" \
    --camera-fps "$CAMERA_FPS"
[ "$CAMERA_MJPEG" = false ] || set -- "$@" --camera-mjpeg
exec "$@"
