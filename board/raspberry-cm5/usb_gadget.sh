#!/bin/sh
set -eu

G=/sys/kernel/config/usb_gadget/seesharp
modprobe libcomposite
mkdir -p "$G"
cd "$G"

echo 0x1d6b > idVendor
echo 0x0104 > idProduct
echo 0x0200 > bcdUSB
echo 0x0100 > bcdDevice

mkdir -p strings/0x409
echo "$(cat /proc/device-tree/serial-number 2>/dev/null | tr -d '\000' || echo CM5)" \
    > strings/0x409/serialnumber
echo "MTV3" > strings/0x409/manufacturer
echo "SeeSharp Vision USB" > strings/0x409/product

mkdir -p configs/c.1/strings/0x409
echo "CDC ACM stream and USB Ethernet" > configs/c.1/strings/0x409/configuration
echo 120 > configs/c.1/MaxPower
mkdir -p functions/acm.usb0
ln -sf functions/acm.usb0 configs/c.1/acm.usb0
mkdir -p functions/ecm.usb0
echo "02:00:00:00:07:02" > functions/ecm.usb0/dev_addr
echo "02:00:00:00:07:01" > functions/ecm.usb0/host_addr
ln -sf functions/ecm.usb0 configs/c.1/ecm.usb0

if [ ! -e UDC ] || [ ! -s UDC ]; then
    UDC="$(ls /sys/class/udc | head -n 1)"
    [ -n "$UDC" ] || { echo "No USB device controller found" >&2; exit 1; }
    echo "$UDC" > UDC
fi

for attempt in 1 2 3 4 5; do
    [ -e /sys/class/net/usb0 ] && break
    sleep 1
done
ip link set usb0 up
ip address replace 192.168.7.2/24 dev usb0
