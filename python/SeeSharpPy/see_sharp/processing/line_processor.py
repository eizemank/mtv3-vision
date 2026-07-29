"""Line detection processor using Canny edge detection and Hough transform.

Ported from C++ LineProcessor (line_processor.cpp).
Self-registers via @register_processor decorator (OCP).
"""

import cv2
import numpy as np

from see_sharp.config import config_keys
from see_sharp.helper.frame_helper import get_gray_frame
from see_sharp.model.detected_object_meta import DetectedObjectMeta
from see_sharp.model.general_params import ProcessingType
from see_sharp.model.line_params import LineParams
from see_sharp.processing.frame_processor import FrameProcessor
from see_sharp.processing.processor_registry import register_processor


@register_processor(ProcessingType.LINE_DETECTION)
class LineProcessor(FrameProcessor):
    """Detects lines using Canny edge detection followed by probabilistic Hough transform."""

    def __init__(self, params: LineParams) -> None:
        self._params = params

    @classmethod
    def from_config(cls, config: dict) -> "LineProcessor":
        """Create a LineProcessor from the application config."""
        params = LineParams.from_config(config[config_keys.LINE_DETECTION])
        return cls(params)

    def process(self, frame: np.ndarray) -> tuple[np.ndarray, list[DetectedObjectMeta]]:
        """Detect lines in the frame.

        Args:
            frame: Input BGR image.

        Returns:
            Tuple of (frame with drawn lines, list of line metadata).
        """
        result_frame = frame.copy()
        metadata: list[DetectedObjectMeta] = []

        gray = get_gray_frame(frame)
        edges = cv2.Canny(
            gray,
            self._params.canny_threshold1,
            self._params.canny_threshold2,
            apertureSize=self._params.aperture_size,
            L2gradient=self._params.use_l2_gradient,
        )

        lines = cv2.HoughLinesP(
            edges,
            self._params.rho,
            self._params.theta,
            self._params.threshold,
            minLineLength=self._params.min_line_length,
            maxLineGap=self._params.max_line_gap,
        )

        if lines is not None:
            for line in lines:
                x1, y1, x2, y2 = line[0]
                cv2.line(result_frame, (x1, y1), (x2, y2), (255, 255, 255), 1)

                meta = DetectedObjectMeta(
                    id=0,
                    center_x=(x1 + x2) / 2.0,
                    center_y=(y1 + y2) / 2.0,
                    area=float(np.linalg.norm([x2 - x1, y2 - y1])),
                    bbox_x=min(x1, x2),
                    bbox_y=min(y1, y2),
                    bbox_w=abs(x2 - x1),
                    bbox_h=abs(y2 - y1),
                )
                metadata.append(meta)

        return result_frame, metadata
