#!/usr/bin/env python3
import struct
import sys

import serial


def crc16_ccitt(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


device = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB1"
port = serial.Serial(device, baudrate=115200, timeout=1)
buffer = bytearray()
print("listening:", device)

while True:
    buffer.extend(port.read(4096))
    while len(buffer) >= 8:
        marker = buffer.find(b"\xaa\x55")
        if marker < 0:
            del buffer[:-1]
            break
        del buffer[:marker]
        if len(buffer) < 5:
            break
        payload_size = struct.unpack_from("<H", buffer, 2)[0]
        packet_size = payload_size + 8
        if len(buffer) < packet_size:
            break
        packet = bytes(buffer[:packet_size])
        del buffer[:packet_size]
        message_id = packet[4]
        payload = packet[5:5 + payload_size]
        expected_crc = struct.unpack_from("<H", packet, 5 + payload_size)[0]
        if packet[-1] != 0x55 or crc16_ccitt(packet[4:5 + payload_size]) != expected_crc:
            print("invalid frame")
            continue
        if len(payload) < 14:
            continue
        frame_id, timestamp_ms, width, height, count, _ = struct.unpack_from("<IIHHBB", payload)
        print(
            "frame={} type=0x{:02x} objects={} image={}x{} timestamp_ms={}".format(
                frame_id, message_id, count, width, height, timestamp_ms
            )
        )
