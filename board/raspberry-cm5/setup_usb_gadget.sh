#!/bin/sh
set -eu

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0" >&2
    exit 1
fi

BOOT_DIR=/boot/firmware
[ -d "$BOOT_DIR" ] || BOOT_DIR=/boot
CONFIG="$BOOT_DIR/config.txt"

if ! grep -Eq '^dtoverlay=dwc2([,].*)?$' "$CONFIG"; then
    printf '\n# SeeSharp USB device mode\ndtoverlay=dwc2,dr_mode=peripheral\n' >> "$CONFIG"
fi

install -m 0644 "$(dirname "$0")/systemd/seesharp-usb-gadget.service" \
    /etc/systemd/system/seesharp-usb-gadget.service
install -m 0755 "$(dirname "$0")/usb_gadget.sh" /usr/local/sbin/seesharp-usb-gadget
systemctl daemon-reload
systemctl enable seesharp-usb-gadget.service

echo "USB CDC + Ethernet gadget installed. Reboot CM5 and verify /dev/ttyGS0 and usb0."
