"""Processing manager - delegates frame processing to an injected processor.

DIP: Receives a FrameProcessor via constructor injection instead of
creating one internally. The caller (composition root in main.py)
is responsible for creating and injecting the correct processor.
"""

import numpy as np

from see_sharp.model.detected_object_meta import DetectedObjectMeta
from see_sharp.processing.frame_processor import FrameProcessor


class ProcessingManager:
    """Manages frame processing by delegating to an injected processor.

    This class exists as a stable interface for the pipeline to call,
    decoupling the pipeline from processor creation logic.
    """

    def __init__(self, processor: FrameProcessor) -> None:
        """Initialize with an injected processor.

        Args:
            processor: The frame processor to delegate to.
        """
        self._processor = processor

    def process_frame(self, frame: np.ndarray) -> tuple[np.ndarray, list[DetectedObjectMeta]]:
        """Process a frame using the injected processor.

        Args:
            frame: Input BGR image.

        Returns:
            Tuple of (result_frame, metadata_list).
        """
        return self._processor.process(frame)
