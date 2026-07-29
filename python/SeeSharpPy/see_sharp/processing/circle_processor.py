"""Circle detection processor using Hough circle transform.

Ported from C++ CircleProcessor (circle_processor.cpp).
SRP fix: Removed hardcoded static variables from C++ version -
all parameters now come from config exclusively.
"""

import cv2
import numpy as np

from see_sharp.config import config_keys
from see_sharp.helper.frame_helper import get_gray_frame
from see_sharp.model.circle_params import CircleParams
from see_sharp.model.detected_object_meta import DetectedObjectMeta
from see_sharp.model.general_params import ProcessingType
from see_sharp.processing.frame_processor import FrameProcessor
from see_sharp.processing.processor_registry import register_processor

GAUSSIAN_BLUR_KERNEL = (9, 9)
GAUSSIAN_BLUR_SIGMA = 2


@register_processor(ProcessingType.CIRCLE_DETECTION)
class CircleProcessor(FrameProcessor):
    """Detects circles using Gaussian blur followed by Hough circle transform."""

    def __init__(self, params: CircleParams) -> None:
        self._params = params

    @classmethod
    def from_config(cls, config: dict) -> "CircleProcessor":
        """Create a CircleProcessor from the application config."""
        params = CircleParams.from_config(config[config_keys.CIRCLE_DETECTION])
        return cls(params)

    def process(self, frame: np.ndarray) -> tuple[np.ndarray, list[DetectedObjectMeta]]:
        """Detect circles in the frame.

        Args:
            frame: Input BGR image.

        Returns:
            Tuple of (frame with drawn circles, list of circle metadata).
        """
        result_frame = frame.copy()
        metadata: list[DetectedObjectMeta] = []

        gray = get_gray_frame(frame)
        blurred = cv2.GaussianBlur(gray, GAUSSIAN_BLUR_KERNEL, GAUSSIAN_BLUR_SIGMA)

        circles = cv2.HoughCircles(
            blurred,
            cv2.HOUGH_GRADIENT,
            dp=1,
            minDist=self._params.distance,
            param1=self._params.hough_param1,
            param2=self._params.hough_param2,
            minRadius=int(self._params.min_radius),
            maxRadius=int(self._params.max_radius),
        )

        if circles is not None:
            circles = np.uint16(np.around(circles))
            for circle in circles[0]:
                center_x, center_y, radius = int(circle[0]), int(circle[1]), int(circle[2])
                cv2.circle(result_frame, (center_x, center_y), radius, (0, 255, 0), 2)
                cv2.circle(result_frame, (center_x, center_y), 2, (0, 0, 255), 3)

                meta = DetectedObjectMeta(
                    id=0,
                    center_x=float(center_x),
                    center_y=float(center_y),
                    area=float(np.pi * radius * radius),
                    bbox_x=center_x - radius,
                    bbox_y=center_y - radius,
                    bbox_w=radius * 2,
                    bbox_h=radius * 2,
                )
                metadata.append(meta)

        return result_frame, metadata
