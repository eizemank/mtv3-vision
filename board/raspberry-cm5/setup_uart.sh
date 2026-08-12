#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

BOOT_DIR=/boot/firmware
[ -d "$BOOT_DIR" ] || BOOT_DIR=/boot
CONFIG="$BOOT_DIR/config.txt"
CMDLINE="$BOOT_DIR/cmdline.txt"

if [ ! -f "$CONFIG" ] || [ ! -f "$CMDLINE" ]; then
    echo "Raspberry Pi boot configuration was not found" >&2
    exit 1
fi

if grep -q '^enable_uart=' "$CONFIG"; then
    sed -i 's/^enable_uart=.*/enable_uart=1/' "$CONFIG"
else
    printf '\n# SeeSharp physical UART on GPIO14 (TXD) and GPIO15 (RXD)\nenable_uart=1\n' >> "$CONFIG"
fi

sed -i -E \
    's/(^|[[:space:]])console=(serial0|ttyAMA[0-9]+|ttyS[0-9]+),[^[:space:]]+//g; s/[[:space:]]+/ /g; s/^ //; s/ $//' \
    "$CMDLINE"

systemctl disable --now serial-getty@serial0.service 2>/dev/null || true
systemctl mask serial-getty@serial0.service 2>/dev/null || true

echo "UART enabled. Reboot, then verify with:"
echo "  readlink -f /dev/serial0"
echo "  stty -F /dev/serial0 115200 raw -echo"
echo "Physical pins: GPIO14/TXD (pin 8), GPIO15/RXD (pin 10), GND (pin 6)."
echo "Use a 3.3 V UART or a suitable half-duplex transceiver; never connect 5 V logic."
