#!/usr/bin/env python3
import json
import sys

import websocket


url = sys.argv[1] if len(sys.argv) > 1 else "ws://127.0.0.1:5002/metadata"
connection = websocket.create_connection(url, timeout=5)
print("connected:", url)

try:
    while True:
        frame = json.loads(connection.recv())
        detections = frame.get("detections", [])
        print(
            "frame={} detector={} objects={}".format(
                frame.get("frame_id"), frame.get("detector"), len(detections)
            )
        )
finally:
    connection.close()
