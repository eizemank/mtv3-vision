#!/bin/sh

set -eu

CONFIGFS=/sys/kernel/config
ROOT=$CONFIGFS/usb_gadget
G=$ROOT/seesharp
USB_ADDRESS=${USB_ADDRESS:-192.168.7.2/24}

fail() {
    echo "usb gadget: $*" >&2
    exit 1
}

mount_configfs() {
    [ -d "$ROOT" ] && return
    mkdir -p "$CONFIGFS"
    grep -qs " $CONFIGFS configfs " /proc/mounts ||
        mount -t configfs none "$CONFIGFS"
    [ -d "$ROOT" ] || fail "CONFIG_USB_CONFIGFS is not available"
}

unbind_all() {
    for gadget in "$ROOT"/*; do
        [ -d "$gadget" ] || continue
        [ -e "$gadget/UDC" ] || continue
        [ -s "$gadget/UDC" ] && printf '\n' > "$gadget/UDC" || true
    done
}

stop_gadget() {
    if [ -e "$G/UDC" ] && [ -s "$G/UDC" ]; then
        printf '\n' > "$G/UDC"
    fi
    ip link set usb0 down 2>/dev/null || true
}

serial_number() {
    for source in /sys/firmware/devicetree/base/serial-number \
                  /proc/device-tree/serial-number /etc/machine-id; do
        if [ -r "$source" ]; then
            tr -d '\000\r\n' < "$source"
            return
        fi
    done
    echo RV1126
}

start_gadget() {
    [ "$(id -u)" = 0 ] || fail "root privileges are required"
    modprobe libcomposite 2>/dev/null || true
    mount_configfs
    unbind_all

    mkdir -p "$G"
    cd "$G"
    echo 0x1d6b > idVendor
    echo 0x0104 > idProduct
    echo 0x0200 > bcdUSB
    echo 0x0100 > bcdDevice

    mkdir -p strings/0x409
    serial_number > strings/0x409/serialnumber
    echo "MTV3" > strings/0x409/manufacturer
    echo "SeeSharp Vision RV1126" > strings/0x409/product

    mkdir -p configs/c.1/strings/0x409
    echo "CDC ACM stream and RNDIS Ethernet" > configs/c.1/strings/0x409/configuration
    echo 250 > configs/c.1/MaxPower

    mkdir -p functions/acm.usb0
    ln -sf "$G/functions/acm.usb0" "$G/configs/c.1/acm.usb0"

    mkdir -p functions/rndis.usb0
    echo "02:00:00:00:07:02" > functions/rndis.usb0/dev_addr
    echo "02:00:00:00:07:01" > functions/rndis.usb0/host_addr
    [ ! -e functions/rndis.usb0/class ] || echo 0xE0 > functions/rndis.usb0/class
    [ ! -e functions/rndis.usb0/subclass ] || echo 0x01 > functions/rndis.usb0/subclass
    [ ! -e functions/rndis.usb0/protocol ] || echo 0x03 > functions/rndis.usb0/protocol
    ln -sf "$G/functions/rndis.usb0" "$G/configs/c.1/rndis.usb0"

    mkdir -p os_desc
    echo 1 > os_desc/use
    echo 0xcd > os_desc/b_vendor_code
    echo MSFT100 > os_desc/qw_sign
    ln -sf "$G/configs/c.1" "$G/os_desc/c.1"

    UDC=$(ls /sys/class/udc 2>/dev/null | head -n 1)
    [ -n "$UDC" ] || fail "no USB device controller; check OTG/device-tree mode"
    echo "$UDC" > UDC

    for attempt in 1 2 3 4 5; do
        [ -e /sys/class/net/usb0 ] && [ -e /dev/ttyGS0 ] && break
        sleep 1
    done
    [ -e /dev/ttyGS0 ] || fail "CDC ACM device /dev/ttyGS0 was not created"
    [ -e /sys/class/net/usb0 ] || fail "RNDIS interface usb0 was not created"
    ip link set usb0 up
    ip address replace "$USB_ADDRESS" dev usb0
    echo "usb gadget: /dev/ttyGS0 and usb0 ($USB_ADDRESS) are ready"
}

case "${1:-start}" in
    start) start_gadget ;;
    stop) mount_configfs; stop_gadget ;;
    restart) mount_configfs; stop_gadget; start_gadget ;;
    status)
        [ -e "$G/UDC" ] && cat "$G/UDC" || true
        [ -e /dev/ttyGS0 ] && echo /dev/ttyGS0
        [ -e /sys/class/net/usb0 ] && ip address show dev usb0
        ;;
    *) fail "usage: $0 {start|stop|restart|status}" ;;
esac
