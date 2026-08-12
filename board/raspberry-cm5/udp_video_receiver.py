#!/usr/bin/env python3
import socket
import struct
import sys
import time

import cv2
import numpy as np


port = int(sys.argv[1]) if len(sys.argv) > 1 else 5001
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("0.0.0.0", port))
sock.settimeout(0.5)
frames = {}

while True:
    try:
        packet, _ = sock.recvfrom(65535)
    except socket.timeout:
        continue
    if len(packet) < 32 or packet[:4] != b"MTV3":
        continue
    version, codec, header_size, frame_id, timestamp_ms, total_size, \
        chunk_index, chunk_count, payload_size, width, height, flags = \
        struct.unpack("!BBHIIIHHHHHH", packet[4:32])
    if version != 1 or codec != 1 or header_size != 32:
        continue
    payload = packet[header_size:header_size + payload_size]
    state = frames.setdefault(frame_id, {
        "created": time.monotonic(), "chunks": {}, "count": chunk_count,
        "size": total_size
    })
    state["chunks"][chunk_index] = payload
    if len(state["chunks"]) == state["count"]:
        jpeg = b"".join(state["chunks"][index]
                         for index in range(state["count"]))
        del frames[frame_id]
        if len(jpeg) != state["size"]:
            continue
        image = cv2.imdecode(np.frombuffer(jpeg, np.uint8), cv2.IMREAD_COLOR)
        if image is not None:
            cv2.imshow("SeeSharp UDP video", image)
            if cv2.waitKey(1) == 27:
                break
    now = time.monotonic()
    for expired_id in [key for key, value in frames.items()
                       if now - value["created"] > 1.0]:
        del frames[expired_id]

cv2.destroyAllWindows()
