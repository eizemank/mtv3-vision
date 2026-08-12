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
    "eeprom_file": "dxl_eeprom.bin"
  }
}
```

Restart `mainCV` after changing this section. Detector parameters and mode can
still be reloaded while the process is running.

## HTTP video

The built-in HTTP server listens on port 8081:

- `/` — live preview and configuration UI;
- `/preview.jpg` — latest annotated JPEG;
- `/config` — active JSON configuration.

The page requests `/preview.jpg` periodically. This is intentionally simpler
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
