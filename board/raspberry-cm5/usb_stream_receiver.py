#!/usr/bin/env python3
import json
import struct
import sys

import cv2
import numpy as np
import serial


device = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM0"
port = serial.Serial(device, timeout=1)
buffer = bytearray()

while True:
    buffer.extend(port.read(65536))
    while len(buffer) >= 16:
        marker = buffer.find(b"MTVU")
        if marker < 0:
            del buffer[:-3]
            break
        if marker:
            del buffer[:marker]
        if len(buffer) < 16:
            break
        version, message_type, flags, frame_id, payload_size = struct.unpack(
            "!BBHII", buffer[4:16])
        if version != 1 or payload_size > 32 * 1024 * 1024:
            del buffer[0]
            continue
        record_size = 16 + payload_size
        if len(buffer) < record_size:
            break
        payload = bytes(buffer[16:record_size])
        del buffer[:record_size]
        if message_type == 1:
            metadata = json.loads(payload)
            print(f"frame={frame_id} detector={metadata['detector']} "
                  f"objects={len(metadata['detections'])}")
        elif message_type == 2:
            image = cv2.imdecode(np.frombuffer(payload, np.uint8),
                                 cv2.IMREAD_COLOR)
            if image is not None:
                cv2.imshow("SeeSharp USB stream", image)
                if cv2.waitKey(1) == 27:
                    raise SystemExit
