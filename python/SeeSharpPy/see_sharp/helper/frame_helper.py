"""Frame preprocessing utilities."""

import cv2
import numpy as np


def get_gray_frame(frame: np.ndarray) -> np.ndarray:
    """Convert a BGR frame to grayscale.

    If the frame is already single-channel, returns a copy.

    Args:
        frame: Input BGR or grayscale image.

    Returns:
        Grayscale image.
    """
    if len(frame.shape) == 3 and frame.shape[2] == 3:
        return cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    return frame.copy()
