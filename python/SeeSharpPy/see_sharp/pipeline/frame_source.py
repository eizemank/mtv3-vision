"""Abstract frame source interface.

DIP: The pipeline depends on this abstraction, not on cv2.VideoCapture.
This enables testing with mock sources and supporting different inputs
(camera, video file, image sequence) without changing pipeline code.
"""

from abc import ABC, abstractmethod

import numpy as np


class FrameSource(ABC):
    """Abstract interface for providing video frames."""

    @abstractmethod
    def read(self) -> tuple[bool, np.ndarray]:
        """Read the next frame.

        Returns:
            Tuple of (success, frame). If success is False, the frame
            data is undefined and the source may be exhausted.
        """
        ...

    @abstractmethod
    def release(self) -> None:
        """Release the frame source and free any resources."""
        ...

    @abstractmethod
    def is_opened(self) -> bool:
        """Check if the frame source is successfully opened."""
        ...
