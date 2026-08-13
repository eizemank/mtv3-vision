# SeeSharp transports on Raspberry CM5

SeeSharp publishes one immutable `VisionFrame` after each processed frame.
Transports are independent: a disabled or unavailable transport does not stop
camera capture or recognition.

## Configuration

The `transports` section in `config.json` controls metadata outputs:

```json
"transports": {
  "udp_metadata": {
    "enabled": true,
    "host": "192.168.1.50",
    "port": 5000
  },
  "uart_dxl": {
    "enabled": true,
    "device": "/dev/serial0",
    "baud": 115200,
    "id": 100,
    "rs485": false,
    "startup_push": true,
    "push_interval_ms": 33,
    "eeprom_file": "dxl_eeprom.bin"
  }
}
```

`startup_push` starts autonomous status packets only after the first processed
frame has populated the Control Table. This supports the 12-second power-on to
first-detection-packet requirement. Disable it on a shared bus unless the bus
master provides a collision-free transmission window.

Restart `mainCV` after changing this section. Detector parameters and mode can
still be reloaded while the process is running.

## HTTP video

The built-in HTTP server listens on port 8081:

- `/` — live preview and configuration UI;
- `/preview.jpg` — latest annotated JPEG;
- `/source.jpg` — latest source-camera JPEG;
- `/config` — active JSON configuration.
- `/admin/status`, `/admin/processes`, `/admin/network` — token-protected CM5
  system information.
- `POST /admin` — token-protected process, network, file and terminal actions.

The page requests `/source.jpg` and `/preview.jpg` periodically and displays
the source and annotated result simultaneously. This is intentionally simpler
and more robust on CM5 than keeping a long multipart MJPEG connection. It also
allows any client to select its own refresh rate.

## UDP metadata

One UTF-8 JSON datagram is emitted for every processed frame. Coordinates in
`center` and `bbox` are normalized to `0.0..1.0`; `area` remains in processor
units for backward compatibility.

```json
{
  "version": "1.0",
  "msg_type": "detection_frame",
  "frame_id": 123,
  "timestamp_ms": 456789,
  "image_size": [640, 480],
  "detector": "aruco",
  "inference_ms": 2.1,
  "fps": 30.0,
  "detections": [
    {
      "class_id": 42,
      "confidence": 1.0,
      "center": [0.5, 0.5],
      "bbox": {"x": 0.5, "y": 0.5, "w": 0.2, "h": 0.2},
      "area": 12000.0
    }
  ]
}
```

Quick receiver:

```bash
nc -u -l 5000
```

UDP is non-blocking. Lost datagrams are not retransmitted; `frame_id` lets the
receiver detect loss. Use UART polling when deterministic request/response is
required. A datagram contains at most 200 detections; `truncated: true` marks
frames that exceeded this safety limit.

## UDP video

Annotated frames can be sent independently from metadata by a dedicated
worker. JPEG encoding and UDP transmission never run in the pipeline thread;
if the network is slower than capture, an old queued frame is replaced by the
newest one.

```json
"udp_video": {
  "enabled": true,
  "host": "192.168.1.50",
  "port": 5001,
  "jpeg_quality": 80,
  "packet_size": 1400,
  "max_fps": 15
}
```

Each JPEG is split into datagrams to avoid IP fragmentation. Every datagram
starts with a 32-byte network-byte-order header:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `MTV3` |
| 4 | 1 | version | `1` |
| 5 | 1 | codec | `1` = JPEG |
| 6 | 2 | header_size | `32` |
| 8 | 4 | frame_id | Frame sequence number |
| 12 | 4 | timestamp_ms | Monotonic timestamp |
| 16 | 4 | total_size | Complete JPEG size |
| 20 | 2 | chunk_index | Zero-based fragment number |
| 22 | 2 | chunk_count | Number of fragments |
| 24 | 2 | payload_size | Bytes following the header |
| 26 | 2 | width | Image width |
| 28 | 2 | height | Image height |
| 30 | 2 | flags | Reserved, currently zero |

The receiver groups packets by `frame_id`, orders them by `chunk_index`, and
decodes JPEG only after all `chunk_count` fragments arrive. UDP does not
retransmit lost fragments, so an incomplete frame must be discarded.

Example receiver (requires `python3-opencv` and `python3-numpy`):

```bash
python3 board/raspberry-cm5/udp_video_receiver.py 5001
```

## UART Dynamixel Protocol 1.0

The implementation follows the agreed CM5 virtual-device protocol:

- header `FF FF` and one-byte DXL checksum;
- PING `0x01`, READ `0x02`, WRITE `0x03`, REG_WRITE `0x04`, ACTION `0x05`;
- FACTORY_RESET `0x06` restores Control Table defaults (ID 100, baud index 16);
- REBOOT `0x08` is acknowledged but does not execute an OS reboot;
- broadcast ID `0xFE` is accepted without a Status Packet;
- polling and Push mode through addresses `0x13`/`0x14`;
- Detector Type at `0x10` switches the live SeeSharp processor and persists
  the new `processing_mode` in `config.json`.

Control Table bytes `0x03..0x07` are persisted in `eeprom_file`. ID and baud
changes take effect fully after restarting the process, as specified for the
EEPROM region.

`rs485: true` enables the standard Linux `TIOCSRS485` direction-control ioctl.
Use it with a UART/transceiver whose driver supports automatic RTS direction.
For a transceiver with a GPIO DE/RE pin, configure that pin in the device tree
or keep `rs485: false` and control direction externally.

### Control Table capacity

The UART object formats match the agreed specification. The physical detection
region is `0x3C..0xFF` (196 bytes), which is stricter than the 253-byte DXL
packet payload limit:

| Detector | Object bytes | Maximum stored objects |
|---|---:|---:|
| NN classification | 14 | 14 |
| ArUco | 30 | 6 |
| Blob | 12 | 15 (configured limit) |
| Line | 12 | 15 (configured limit) |
| Circle | 8 | 15 (configured limit) |

If detections exceed the available slots, bit `OVERFLOW` at `0x27` is set.
ArUco corners are currently reconstructed from the detected marker bounding
rectangle and pose fields are zero because the legacy processor metadata does
not yet expose the original four corners or calibrated pose.

### UART setup on Raspberry Pi OS

Connect the physical UART to GPIO14/TXD (header pin 8), GPIO15/RXD (pin 10)
and GND (pin 6). These are 3.3 V signals. For a one-wire Dynamixel bus use a
suitable half-duplex buffer/transceiver; do not connect a 5 V data line
directly to CM5 GPIO.

The supplied idempotent script enables the UART, removes the Linux serial
console from it and disables `serial-getty`:

```bash
cd ~/mtv3-vision
sudo sh board/raspberry-cm5/setup_uart.sh
sudo reboot
```

After reboot, verify the stable Raspberry Pi alias used by SeeSharp:

```bash
readlink -f /dev/serial0
stty -F /dev/serial0 115200 raw -echo
```

For manual startup as a non-root user, add it to `dialout` once with
`sudo usermod -aG dialout "$USER"` and log in again. The supplied systemd unit
already requests the `dialout` supplementary group.

## USB metadata, video and web UI

CM5 exposes a composite CDC ACM and CDC ECM USB gadget. USB Ethernet uses
`192.168.7.2/24`; assign `192.168.7.1/24` to the host and open
`http://192.168.7.2:8081/` for the complete configuration and video UI.
SeeSharp also writes JSON
metadata and annotated JPEG frames to `/dev/ttyGS0` from a dedicated worker,
so a slow host does not block capture or detection.

```json
"usb_stream": {
  "enabled": true,
  "device": "/dev/ttyGS0",
  "metadata": true,
  "video": true,
  "jpeg_quality": 80,
  "max_fps": 15
}
```

The byte stream consists of records with a 16-byte big-endian header:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `MTVU` |
| 4 | 1 | version | `1` |
| 5 | 1 | message_type | `1` JSON metadata, `2` JPEG video |
| 6 | 2 | flags | Reserved, zero |
| 8 | 4 | frame_id | Associates metadata and image |
| 12 | 4 | payload_size | Number of following bytes |

Metadata uses the same `detection_frame` JSON as UDP metadata. A video payload
is one complete JPEG and does not require UDP-style fragment assembly.

The carrier board port must support USB device/peripheral mode. Install the
gadget on CM5 and reboot:

```bash
cd ~/mtv3-vision
sudo sh board/raspberry-cm5/setup_usb_gadget.sh
sudo reboot
ls -l /dev/ttyGS0
```

The Linux host normally enumerates it as `/dev/ttyACM0`:

```bash
sudo apt install python3-serial python3-opencv python3-numpy
python3 board/raspberry-cm5/usb_stream_receiver.py /dev/ttyACM0
```

On Windows pass the assigned COM port, for example `COM7`. The receiver prints
detector/object counts and displays annotated frames. Press `Esc` to exit.
