"""ArUco marker detection processor.

Ported from C++ ArucoProcessor (aruco_processor.cpp).
Handles OpenCV 4.7+ API changes for ArUco detection.
"""

import cv2
import numpy as np

from see_sharp.config import config_keys
from see_sharp.model.detected_object_meta import DetectedObjectMeta
from see_sharp.model.general_params import ProcessingType
from see_sharp.processing.frame_processor import FrameProcessor
from see_sharp.processing.processor_registry import register_processor

ARUCO_DICTIONARY = cv2.aruco.DICT_4X4_50


@register_processor(ProcessingType.ARUCO_DETECTION)
class ArucoProcessor(FrameProcessor):
    """Detects and visualizes ArUco markers using OpenCV's ArUco module."""

    def __init__(self) -> None:
        dictionary = cv2.aruco.getPredefinedDictionary(ARUCO_DICTIONARY)
        # OpenCV 4.7+ uses ArucoDetector class
        try:
            self._detector = cv2.aruco.ArucoDetector(dictionary)
        except AttributeError:
            self._detector = None
            self._dictionary = dictionary

    @classmethod
    def from_config(cls, config: dict) -> "ArucoProcessor":
        """Create an ArucoProcessor from the application config."""
        return cls()

    def process(self, frame: np.ndarray) -> tuple[np.ndarray, list[DetectedObjectMeta]]:
        """Detect ArUco markers in the frame.

        Args:
            frame: Input BGR image.

        Returns:
            Tuple of (frame with drawn markers, list of marker metadata).
        """
        result_frame = frame.copy()
        metadata: list[DetectedObjectMeta] = []

        # Detect markers (API differs between OpenCV versions)
        if self._detector is not None:
            corners, ids, _ = self._detector.detectMarkers(frame)
        else:
            corners, ids, _ = cv2.aruco.detectMarkers(frame, self._dictionary)

        if ids is not None:
            cv2.aruco.drawDetectedMarkers(result_frame, corners, ids)

            for i, marker_corners in enumerate(corners):
                pts = marker_corners[0]
                center_x = float(np.mean(pts[:, 0]))
                center_y = float(np.mean(pts[:, 1]))

                x_min = int(np.min(pts[:, 0]))
                y_min = int(np.min(pts[:, 1]))
                x_max = int(np.max(pts[:, 0]))
                y_max = int(np.max(pts[:, 1]))

                meta = DetectedObjectMeta(
                    id=int(ids[i][0]),
                    center_x=center_x,
                    center_y=center_y,
                    area=float(cv2.contourArea(pts.astype(np.int32))),
                    bbox_x=x_min,
                    bbox_y=y_min,
                    bbox_w=x_max - x_min,
                    bbox_h=y_max - y_min,
                )
                metadata.append(meta)

        return result_frame, metadata
