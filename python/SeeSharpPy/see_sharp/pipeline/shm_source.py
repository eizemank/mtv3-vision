"""SHM frame source for MTV3 (RV1126).

Reads the NV12 ring written by `mtv3_cam_daemon --shm` into /dev/shm/mtv3cam
and converts frames to BGR. Drop-in FrameSource replacement for CameraSource.

Layout (written by the daemon, little-endian, 64-byte header + slots):
    u32 magic ("M3SH" = 0x4D335348), version, w, h, stride, fmt(0=NV12),
    nslot, slot_size, seq, ts_ms, reserved[6]
`seq` counts completed frames; the newest frame is in slot (seq-1) % nslot.

NOTE: deliberately written in python-3.6-compatible syntax (buildroot target);
py36ify.py skips this file's annotations by construction.
"""

import mmap
import os
import struct
import time
from typing import Optional, Tuple  # noqa: F401 (kept for 3.6 targets)

import cv2
import numpy as np

from see_sharp.pipeline.frame_source import FrameSource

_HDR = struct.Struct("<10I")  # magic, version, w, h, stride, fmt, nslot, slot_size, seq, ts_ms
_HDR_BYTES = 64
_MAGIC = 0x4D335348
_SEQ_IDX = 8


class ShmSource(FrameSource):
    """Frame source reading the daemon's shared-memory NV12 ring."""

    def __init__(self, path="/dev/shm/mtv3cam", timeout_s=5.0):
        # type: (str, float) -> None
        """Open the ring, waiting up to timeout_s for the daemon to create it."""
        self._path = path
        self._timeout = timeout_s
        self._mm = None  # type: Optional[mmap.mmap]
        self._last_seq = 0
        self._w = self._h = self._stride = self._nslot = self._slot = 0
        self._open_wait()

    def _open_wait(self):
        # type: () -> None
        deadline = time.monotonic() + self._timeout
        while time.monotonic() < deadline:
            try:
                fd = os.open(self._path, os.O_RDONLY)
            except OSError:
                time.sleep(0.1)
                continue
            try:
                size = os.fstat(fd).st_size
                if size <= _HDR_BYTES:
                    time.sleep(0.1)
                    continue
                mm = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ)
            finally:
                os.close(fd)
            hdr = _HDR.unpack_from(mm, 0)
            if hdr[0] != _MAGIC:
                mm.close()
                time.sleep(0.1)
                continue
            (_, _, self._w, self._h, self._stride, fmt,
             self._nslot, self._slot, seq, _) = hdr
            if fmt != 0:
                mm.close()
                raise RuntimeError("mtv3cam shm: unsupported fmt %d" % fmt)
            self._mm = mm
            self._last_seq = seq
            return

    def _seq(self):
        # type: () -> int
        return _HDR.unpack_from(self._mm, 0)[_SEQ_IDX]

    def read(self):
        # type: () -> Tuple[bool, np.ndarray]
        """Block until a new frame arrives, return (ok, BGR frame)."""
        empty = np.empty(0, dtype=np.uint8)
        if self._mm is None:
            return False, empty

        deadline = time.monotonic() + self._timeout
        while True:
            seq = self._seq()
            if seq > 0 and seq != self._last_seq:
                break
            if time.monotonic() > deadline:
                return False, empty  # демон умер/завис — пусть pipeline завершится
            time.sleep(0.002)

        for _ in range(3):  # анти-tearing: слот мог быть перезаписан во время копии
            seq = self._seq()
            slot = (seq - 1) % self._nslot
            off = _HDR_BYTES + slot * self._slot
            raw = np.frombuffer(self._mm, dtype=np.uint8,
                                count=self._slot, offset=off).copy()
            if self._seq() - seq < self._nslot - 1:
                self._last_seq = seq
                nv12 = raw.reshape(self._h * 3 // 2, self._stride)[:, : self._w]
                nv12 = np.ascontiguousarray(nv12)
                return True, cv2.cvtColor(nv12, cv2.COLOR_YUV2BGR_NV12)
        return False, empty

    def release(self):
        # type: () -> None
        if self._mm is not None:
            self._mm.close()
            self._mm = None

    def is_opened(self):
        # type: () -> bool
        return self._mm is not None
