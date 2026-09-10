#!/usr/bin/env python3
"""Select CM5 GPIO14/15 DXL transport in an existing SeeSharp config."""

import argparse
import json
from pathlib import Path
import shutil


def configure(path):
    path = Path(path).resolve(strict=True)
    config = json.loads(path.read_text(encoding="utf-8-sig"))
    transports = config.setdefault("transports", {})
    uart = transports.setdefault("uart_dxl", {})
    uart.update(enabled=True, device="/dev/ttyAMA0", baud=115200, id=100,
                rs485=False, startup_push=True, push_interval_ms=33)
    # An independent EEPROM file prevents old ID/baud settings overriding
    # this bench profile. Further DXL EEPROM writes still persist normally.
    uart["eeprom_file"] = str(path.parent / "dxl_gpio14_15_eeprom.bin")
    transports.setdefault("uart_binary", {})["enabled"] = False
    backup = path.with_name(path.name + ".before-uart-dxl")
    if not backup.exists():
        shutil.copy2(path, backup)
    path.write_text(json.dumps(config, ensure_ascii=False, indent=2) + "\n",
                    encoding="utf-8")
    print(f"DXL: /dev/ttyAMA0, 115200 8N1, ID 100, push every 33 ms: {path}")
    print(f"Backup: {backup}. Restart SeeSharp after configuring the UART.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", help="Config actually used by mainCV")
    args = parser.parse_args()
    configure(args.config)
