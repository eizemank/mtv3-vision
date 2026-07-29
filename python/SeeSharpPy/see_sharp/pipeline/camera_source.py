"""Camera frame source - concrete implementation using cv2.VideoCapture.

LSP: Fully substitutable with any other FrameSource implementation.
"""

import cv2
import numpy as np

from see_sharp.pipeline.frame_source import FrameSource


class CameraSource(FrameSource):
    """Frame source that captures from a camera or video file via OpenCV."""

    def __init__(self, device: int = 0, backend: int | None = None) -> None:
        """Open a camera device or video file.

        Args:
            device: Camera device index or video file path.
            backend: Optional OpenCV backend (e.g., cv2.CAP_V4L2).
        """
        if backend is not None:
            self._cap = cv2.VideoCapture(device, backend)
        else:
            self._cap = cv2.VideoCapture(device)

    def read(self) -> tuple[bool, np.ndarray]:
        """Read the next frame from the camera."""
        return self._cap.read()

    def release(self) -> None:
        """Release the camera resource."""
        self._cap.release()

    def is_opened(self) -> bool:
        """Check if the camera is opened."""
        return self._cap.isOpened()
