#!/bin/sh

set -eu

MODE=${1:-}
IFACE=${2:-}
STATE=/run/seesharp-wifi-mode
WPA_CONF=/run/seesharp-wpa.conf
HOSTAPD_CONF=/run/seesharp-hostapd.conf
UDHCPD_CONF=/run/seesharp-udhcpd.conf

fail() {
    echo "network: $*" >&2
    exit 1
}

need() {
    command -v "$1" >/dev/null 2>&1 || fail "$1 is not installed"
}

safe_text() {
    [ -n "$1" ] || fail "empty value"
    if printf '%s' "$1" | grep -q '[[:cntrl:]]'; then
        fail "control characters are not allowed"
    fi
}

safe_address() {
    case "$1" in
        ''|*[!0-9a-fA-F:./]*) fail "invalid address: $1" ;;
    esac
}

[ "$(id -u)" = 0 ] || fail "root privileges are required"
case "$IFACE" in
    ''|*[!a-zA-Z0-9_.-]*) fail "invalid interface" ;;
esac
[ -d "/sys/class/net/$IFACE" ] || fail "interface $IFACE does not exist"
need ip

stop_wifi() {
    killall wpa_supplicant hostapd udhcpd 2>/dev/null || true
}

stop_dhcp() {
    PIDFILE="/run/udhcpc-$IFACE.pid"
    if [ -f "$PIDFILE" ]; then
        kill "$(cat "$PIDFILE")" 2>/dev/null || true
        rm -f "$PIDFILE"
    fi
}

start_dhcp() {
    need udhcpc
    stop_dhcp
    ip addr flush dev "$IFACE"
    ip link set "$IFACE" up
    udhcpc -b -i "$IFACE" -p "/run/udhcpc-$IFACE.pid"
}

case "$MODE" in
    dhcp)
        stop_dhcp
        start_dhcp
        echo dhcp > "$STATE"
        ;;
    static)
        ADDRESS=${3:-}
        GATEWAY=${4:-}
        DNS=${5:-}
        safe_address "$ADDRESS"
        [ -z "$GATEWAY" ] || safe_address "$GATEWAY"
        [ -z "$DNS" ] || safe_address "$DNS"
        stop_dhcp
        ip addr flush dev "$IFACE"
        ip link set "$IFACE" up
        ip addr add "$ADDRESS" dev "$IFACE"
        [ -z "$GATEWAY" ] || ip route replace default via "$GATEWAY" dev "$IFACE"
        [ -z "$DNS" ] || printf 'nameserver %s\n' "$DNS" > /etc/resolv.conf
        echo static > "$STATE"
        ;;
    client)
        SSID=${3:-}
        PASSWORD=${4:-}
        safe_text "$SSID"
        safe_text "$PASSWORD"
        need wpa_supplicant
        need wpa_passphrase
        stop_wifi
        wpa_passphrase "$SSID" "$PASSWORD" > "$WPA_CONF"
        chmod 600 "$WPA_CONF"
        ip link set "$IFACE" up
        wpa_supplicant -B -i "$IFACE" -c "$WPA_CONF"
        start_dhcp
        echo client > "$STATE"
        ;;
    ap)
        SSID=${3:-}
        PASSWORD=${4:-}
        safe_text "$SSID"
        safe_text "$PASSWORD"
        [ "${#PASSWORD}" -ge 8 ] && [ "${#PASSWORD}" -le 63 ] ||
            fail "AP password must contain 8-63 characters"
        need hostapd
        stop_wifi
        cat > "$HOSTAPD_CONF" <<EOF
interface=$IFACE
driver=nl80211
ssid=$SSID
hw_mode=g
channel=6
wpa=2
wpa_passphrase=$PASSWORD
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
EOF
        chmod 600 "$HOSTAPD_CONF"
        ip addr flush dev "$IFACE"
        ip link set "$IFACE" up
        ip addr add 192.168.4.1/24 dev "$IFACE"
        hostapd -B "$HOSTAPD_CONF"
        if command -v udhcpd >/dev/null 2>&1; then
            cat > "$UDHCPD_CONF" <<EOF
start 192.168.4.10
end 192.168.4.100
interface $IFACE
option subnet 255.255.255.0
option router 192.168.4.1
option dns 192.168.4.1
lease_file /run/udhcpd.leases
pidfile /run/udhcpd.pid
EOF
            : > /run/udhcpd.leases
            udhcpd "$UDHCPD_CONF"
        fi
        echo ap > "$STATE"
        ;;
    *)
        fail "usage: $0 {dhcp|static|client|ap} interface [parameters]"
        ;;
esac

echo "network: $MODE configured on $IFACE"
