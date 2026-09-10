#!/usr/bin/env python3
"""Check CM5 DXL PING and Control Table READ from a USB-UART host."""

import argparse
import time


def packet(device_id, instruction, params=b""):
    body = bytes([device_id, len(params) + 2, instruction]) + params
    return b"\xff\xff" + body + bytes([(~sum(body)) & 0xff])


def exchange(port, device_id, instruction, params, response_size, timeout):
    port.reset_input_buffer()
    request = packet(device_id, instruction, params)
    print("TX:", request.hex(" "))
    port.write(request)
    deadline = time.monotonic() + timeout
    buffer = bytearray()
    while time.monotonic() < deadline:
        buffer.extend(port.read(max(1, port.in_waiting)))
        while len(buffer) >= 4:
            if buffer[:2] != b"\xff\xff" or buffer[3] < 2:
                del buffer[0]
                continue
            size = buffer[3] + 4
            if len(buffer) < size:
                break
            reply = bytes(buffer[:size])
            if sum(reply[2:]) & 0xff != 0xff:
                del buffer[0]
                continue
            del buffer[:size]
            if reply[2] != device_id:
                continue
            # Automatic Push packets contain >=12 parameter bytes. These
            # short requests can be checked without changing Push mode.
            if reply[4]:
                raise RuntimeError(f"DXL error 0x{reply[4]:02x}: {reply.hex(' ')}")
            if len(reply) - 6 != response_size:
                continue
            print("RX:", reply.hex(" "))
            return reply[5:-1]
    raise TimeoutError("No matching DXL status: check wiring, baud, ID and mainCV")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USB-UART on the host, e.g. COM7 or /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--id", type=int, default=100, choices=range(254), metavar="0..253")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()
    import serial

    try:
        with serial.Serial(args.port, args.baud, timeout=0.05,
                           write_timeout=args.timeout, rtscts=False,
                           dsrdtr=False, xonxoff=False) as port:
            exchange(port, args.id, 1, b"", 0, args.timeout)
            identity = exchange(port, args.id, 2, b"\x00\x04", 4, args.timeout)
            if identity[:2] != b"\x43\x56" or identity[3] != args.id:
                raise RuntimeError(f"Unexpected model/ID: {identity.hex(' ')}")
            print(f"PASS: PING + READ, model 0x5643, firmware 0x{identity[2]:02x}, ID {identity[3]}")
    except (OSError, serial.SerialException, RuntimeError, TimeoutError) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
