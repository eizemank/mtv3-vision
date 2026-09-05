#!/bin/sh
set -eu

if [ "$(id -u)" -eq 0 ]; then
    echo "Do not run the host UI launcher with sudo." >&2
    echo "If the build directory is already root-owned, repair it with:" >&2
    echo "  sudo chown -R \"\$USER:\$USER\" \"\$HOME/.cache/seesharp\"" >&2
    exit 1
fi

ROOT="$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)"
SOURCE="$ROOT/cpp/SeeSharp"
BUILD="${SEESHARP_HOST_BUILD:-$HOME/.cache/seesharp/build-host-ui}"
DOWNLOAD_YOLO=false
if [ "${1:-}" = "--download-yolo" ]; then
    DOWNLOAD_YOLO=true
elif [ "$#" -gt 0 ]; then
    echo "Usage: $0 [--download-yolo]" >&2
    exit 2
fi

missing=""
for command in cmake c++ pkg-config python3; do
    if ! command -v "$command" >/dev/null 2>&1; then
        missing="$missing $command"
    fi
done
if [ -n "$missing" ]; then
    echo "Missing host build tools:$missing" >&2
    echo "Install them on Ubuntu/Debian/WSL:" >&2
    echo "  sudo apt update" >&2
    echo "  sudo apt install -y build-essential cmake pkg-config libopencv-dev nlohmann-json3-dev python3" >&2
    exit 1
fi
if ! pkg-config --exists opencv4; then
    echo "OpenCV development files are missing." >&2
    echo "Install them with: sudo apt install -y libopencv-dev" >&2
    exit 1
fi

if [ -f "$BUILD/CMakeCache.txt" ] &&
   grep -q 'CMAKE_CXX_COMPILER:FILEPATH=CMAKE_CXX_COMPILER-NOTFOUND' "$BUILD/CMakeCache.txt"; then
    echo "Removing stale CMake cache created before the compiler was installed."
    rm -f "$BUILD/CMakeCache.txt"
    rm -rf "$BUILD/CMakeFiles"
fi

cmake -S "$SOURCE" -B "$BUILD" -DHOST_WEB_UI=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD" --parallel "${SEESHARP_BUILD_JOBS:-4}"

if [ "$DOWNLOAD_YOLO" = true ]; then
    for command in curl python3; do
        if ! command -v "$command" >/dev/null 2>&1; then
            echo "YOLO download requires $command." >&2
            echo "Install prerequisites: sudo apt install -y curl python3" >&2
            exit 1
        fi
    done
    cmake --build "$BUILD" --target download_yolo11n
fi

python3 - "$BUILD/config.json" <<'PY'
import json
import sys
from pathlib import Path

path = Path(sys.argv[1])
config = json.loads(path.read_text())
config["general_params"]["processing_mode"] = "circle_detection"
config["general_params"]["debug_mode"] = True
for transport in config.get("transports", {}).values():
    if isinstance(transport, dict):
        transport["enabled"] = False
path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n")
PY

echo "SeeSharp host UI: http://127.0.0.1:8081/"
echo "Press Ctrl+C to stop."
cd "$BUILD"
exec ./mainCV --config ./config.json
