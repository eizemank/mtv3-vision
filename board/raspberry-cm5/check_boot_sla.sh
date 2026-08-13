#!/bin/sh
set -eu

LIMIT_MS="${1:-12000}"
LINE="$(journalctl -b -u seesharp-cm5.service -o cat --no-pager |
    grep 'BOOT_SLA first_uart_detection_ms=' | tail -n 1 || true)"

if [ -z "$LINE" ]; then
    echo "FAIL: no UART detection marker in current boot" >&2
    exit 1
fi

VALUE="$(printf '%s\n' "$LINE" |
    sed -n 's/.*first_uart_detection_ms=\([0-9][0-9]*\).*/\1/p')"
if [ -z "$VALUE" ]; then
    echo "FAIL: malformed marker: $LINE" >&2
    exit 1
fi

printf 'Kernel-start to first UART detection packet: %s ms (limit %s ms)\n' \
    "$VALUE" "$LIMIT_MS"
systemd-analyze time || true

if [ "$VALUE" -gt "$LIMIT_MS" ]; then
    echo "FAIL: boot SLA exceeded" >&2
    exit 1
fi
echo "PASS: boot SLA met"
