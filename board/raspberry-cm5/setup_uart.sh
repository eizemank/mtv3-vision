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

if [ ! -f "$BOOT_DIR/overlays/uart0-pi5.dtbo" ]; then
    echo "CM5 UART overlay uart0-pi5.dtbo was not found" >&2
    exit 1
fi

# Keep the original files for rollback; do not replace backups on reruns.
[ -f "$CONFIG.before-seesharp-uart" ] || cp -p "$CONFIG" "$CONFIG.before-seesharp-uart"
[ -f "$CMDLINE.before-seesharp-uart" ] || cp -p "$CMDLINE" "$CMDLINE.before-seesharp-uart"

# A final [all] section avoids inheriting an unrelated conditional section.
sed -i '/^# BEGIN SEESHARP UART$/,/^# END SEESHARP UART$/d' "$CONFIG"
cat >> "$CONFIG" <<'EOF'

# BEGIN SEESHARP UART
[all]
enable_uart=1
dtoverlay=uart0-pi5
# END SEESHARP UART
EOF

sed -i -E \
    's/(^|[[:space:]])console=(serial0|ttyAMA[0-9]+|ttyS[0-9]+),[^[:space:]]+//g; s/[[:space:]]+/ /g; s/^ //; s/ $//' \
    "$CMDLINE"

for port in serial0 ttyAMA0; do
    systemctl disable --now "serial-getty@$port.service" 2>/dev/null || true
    systemctl mask "serial-getty@$port.service"
done

echo "UART enabled. Reboot, then verify with:"
echo "  pinctrl get 14 15"
echo "  ls -l /dev/ttyAMA0"
echo "Select /dev/ttyAMA0 in SeeSharp; /dev/serial0 may refer to the debug UART."
echo "Configure DXL with: python3 board/raspberry-cm5/configure_uart_dxl.py /path/to/config.json"
echo "Physical pins: GPIO14/TXD (pin 8), GPIO15/RXD (pin 10), GND (pin 6)."
echo "Use a 3.3 V UART or a suitable half-duplex transceiver; never connect 5 V logic."
