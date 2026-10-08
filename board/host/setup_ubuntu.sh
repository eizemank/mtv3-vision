#!/bin/sh
# Install everything the host build needs on Ubuntu/Debian (native or WSL):
# compiler, CMake, OpenCV (with contrib: aruco + dnn), nlohmann-json, Python,
# curl (YOLO download) and Node.js (web UI unit tests).
#   sh board/host/setup_ubuntu.sh            # build + run dependencies
#   sh board/host/setup_ubuntu.sh --with-tests  # + nodejs for tests/*.test.cjs
set -eu

WITH_TESTS=false
case "${1:-}" in
    --with-tests) WITH_TESTS=true ;;
    "") ;;
    *) echo "Usage: $0 [--with-tests]" >&2; exit 2 ;;
esac

if ! command -v apt-get >/dev/null 2>&1; then
    echo "apt-get not found: this script targets Ubuntu/Debian." >&2
    echo "Install manually: C++17 compiler, cmake >= 3.10, OpenCV 4 (aruco, dnn), nlohmann-json, python3, curl." >&2
    exit 1
fi

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    command -v sudo >/dev/null 2>&1 || { echo "Run as root or install sudo." >&2; exit 1; }
    SUDO="sudo"
fi

PACKAGES="build-essential cmake pkg-config libopencv-dev nlohmann-json3-dev python3 curl"
[ "$WITH_TESTS" = true ] && PACKAGES="$PACKAGES nodejs"

echo "Installing: $PACKAGES"
$SUDO apt-get update
# shellcheck disable=SC2086
$SUDO apt-get install -y $PACKAGES

echo
echo "Toolchain:"
c++ --version | head -1
cmake --version | head -1
pkg-config --modversion opencv4 2>/dev/null | sed 's/^/OpenCV /' || echo "OpenCV: pkg-config opencv4 not found"
[ "$WITH_TESTS" = true ] && node --version | sed 's/^/Node.js /'
echo
echo "Next: sh board/host/run_web_ui.sh"
