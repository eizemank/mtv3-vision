"""Detected single-color blob with computed morphological properties.

Ported from C++ DetectedBlob (detected_blob.hpp / detected_blob.cpp).
Contour properties (area, circularity, inertia, convexity, max axis)
are computed in __post_init__.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field

import cv2
import numpy as np

from see_sharp.model.max_axis import MaxAxis


@dataclass
class DetectedBlob:
    """A single-color blob detected from a contour.

    Properties are computed from the contour on construction.

    Attributes:
        contour: Raw contour points.
        color_pattern_id: Which color pattern matched this blob.
        circularity: (4 * pi * area) / perimeter^2.
        inertia: Minor axis / major axis of fitted ellipse.
        convexity: area / hull_area.
        max_axis: Maximum distance axis across the convex hull.
        area: Contour area.
        hull_area: Area of the convex hull.
        bounding_box: (x, y, w, h) bounding rectangle.
        hull_contour: Convex hull points.
        center: Centroid computed from image moments.
    """

    contour: np.ndarray
    color_pattern_id: int = -1

    # Computed fields
    circularity: float = field(init=False, default=0.0)
    inertia: float = field(init=False, default=0.0)
    convexity: float = field(init=False, default=0.0)
    max_axis: MaxAxis = field(init=False)
    area: float = field(init=False, default=0.0)
    hull_area: float = field(init=False, default=0.0)
    bounding_box: tuple[int, int, int, int] = field(init=False, default=(0, 0, 0, 0))
    hull_contour: np.ndarray = field(init=False)
    center: tuple[float, float] = field(init=False, default=(0.0, 0.0))

    def __post_init__(self) -> None:
        if self.contour is None or len(self.contour) == 0:
            self.max_axis = MaxAxis()
            self.hull_contour = np.array([])
            return

        # Area and bounding box
        self.area = cv2.contourArea(self.contour)
        x, y, w, h = cv2.boundingRect(self.contour)
        self.bounding_box = (x, y, w, h)

        # Convex hull
        self.hull_contour = cv2.convexHull(self.contour)
        self.hull_area = cv2.contourArea(self.hull_contour)

        # Max axis
        self.max_axis = self._get_max_axis()

        # Center from moments
        mu = cv2.moments(self.contour)
        if mu["m00"] != 0:
            self.center = (mu["m10"] / mu["m00"], mu["m01"] / mu["m00"])

        # Circularity
        perimeter = cv2.arcLength(self.contour, True)
        if perimeter > 0:
            self.circularity = (4.0 * math.pi * self.area) / (perimeter * perimeter)

        # Inertia and convexity
        self.inertia = self._calculate_inertia()
        self.convexity = self._calculate_convexity()

    def _calculate_inertia(self) -> float:
        """Minor axis / major axis from fitted ellipse."""
        if len(self.contour) < 5:
            return 0.0
        ellipse = cv2.fitEllipse(self.contour)
        width, height = ellipse[1]
        major = max(width, height)
        minor = min(width, height)
        return minor / major if major > 0 else 0.0

    def _calculate_convexity(self) -> float:
        """Area / hull area."""
        return self.area / (self.hull_area + 1e-5)

    def _get_max_axis(self) -> MaxAxis:
        """Find the maximum distance pair of points in the convex hull."""
        hull_pts = self.hull_contour.reshape(-1, 2)
        if len(hull_pts) < 2:
            return MaxAxis()

        max_dist = 0.0
        start = (0.0, 0.0)
        end = (0.0, 0.0)

        for i in range(len(hull_pts)):
            for j in range(i + 1, len(hull_pts)):
                dist = float(np.linalg.norm(hull_pts[i] - hull_pts[j]))
                if dist > max_dist:
                    max_dist = dist
                    start = (float(hull_pts[i][0]), float(hull_pts[i][1]))
                    end = (float(hull_pts[j][0]), float(hull_pts[j][1]))

        return MaxAxis(start=start, end=end)
