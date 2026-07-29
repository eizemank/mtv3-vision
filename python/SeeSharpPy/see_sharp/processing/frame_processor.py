"""Abstract base class for frame processors.

ISP: Single method interface - processors only need to implement process().
LSP: All implementations return the same tuple type, ensuring substitutability.
"""

from abc import ABC, abstractmethod

import numpy as np

from see_sharp.model.detected_object_meta import DetectedObjectMeta


class FrameProcessor(ABC):
    """Abstract interface for processing video frames.

    Each concrete processor implements a specific computer vision algorithm
    (line detection, circle detection, ArUco markers, etc.) while conforming
    to the same interface.
    """

    @abstractmethod
    def process(self, frame: np.ndarray) -> tuple[np.ndarray, list[DetectedObjectMeta]]:
        """Process a single video frame.

        Args:
            frame: Input BGR image as numpy array.

        Returns:
            A tuple of (result_frame, metadata_list) where result_frame
            is the frame with visualized detections and metadata_list
            contains information about each detected object.
        """
        ...

    @classmethod
    @abstractmethod
    def from_config(cls, config: dict) -> "FrameProcessor":
        """Create a processor instance from the full application config.

        Args:
            config: The complete application configuration dictionary.

        Returns:
            A configured processor instance.
        """
        ...
