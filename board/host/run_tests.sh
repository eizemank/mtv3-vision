#!/bin/sh
# Host test suite for cpp/SeeSharp (Linux/WSL):
#   1. web UI unit tests (node, tests/*.test.cjs)
#   2. blob color mask parity test (g++ + OpenCV core)
#   3. UART protocol / binary transport test on a pty pair (g++ + OpenCV core)
# Usage: sh board/host/run_tests.sh [--no-native]
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE="$ROOT/cpp/SeeSharp"
OUT="${SEESHARP_TEST_BUILD:-$HOME/.cache/seesharp/tests}"
NATIVE=true
[ "${1:-}" = "--no-native" ] && NATIVE=false

failed=0
run() {
    name=$1; shift
    if "$@"; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        failed=$((failed + 1))
    fi
}

if command -v node >/dev/null 2>&1; then
    for test in "$SOURCE"/tests/*.test.cjs; do
        run "$(basename "$test")" node "$test"
    done
else
    echo "SKIP node tests: install nodejs (sh board/host/setup_ubuntu.sh --with-tests)"
fi

if [ "$NATIVE" = true ]; then
    # OpenCV flags: override with SEESHARP_OPENCV_CFLAGS / SEESHARP_OPENCV_LIBS when
    # pkg-config points at a different OpenCV than the CMake build uses
    OPENCV_CFLAGS=${SEESHARP_OPENCV_CFLAGS-$(pkg-config --cflags opencv4 2>/dev/null || echo "-I/usr/include/opencv4")}
    OPENCV_LIBS=${SEESHARP_OPENCV_LIBS-$(pkg-config --libs-only-L opencv4 2>/dev/null) -lopencv_core}
    INCLUDES="-I $SOURCE/include -I $SOURCE/third_party $OPENCV_CFLAGS"
    mkdir -p "$OUT"
    cd "$SOURCE"
    run "blob_color_mask.test.cpp (build)" \
        g++ -std=c++17 $INCLUDES tests/blob_color_mask.test.cpp $OPENCV_LIBS \
            -o "$OUT/blob-color-mask-test"
    [ -x "$OUT/blob-color-mask-test" ] && run "blob_color_mask.test.cpp" "$OUT/blob-color-mask-test"

    run "uart_protocol.test.cpp (build)" \
        g++ -std=c++17 $INCLUDES tests/uart_protocol.test.cpp \
            src/transport/binary_uart_transport.cpp src/transport/dxl_uart_transport.cpp \
            src/platform/serial.cpp $OPENCV_LIBS -pthread -lutil \
            -o "$OUT/uart-protocol-test"
    [ -x "$OUT/uart-protocol-test" ] && run "uart_protocol.test.cpp" "$OUT/uart-protocol-test"
fi

echo
if [ "$failed" -eq 0 ]; then
    echo "All host tests passed."
else
    echo "$failed test step(s) failed." >&2
    exit 1
fi
