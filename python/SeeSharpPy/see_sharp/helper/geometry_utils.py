"""Geometry utility functions.

Ported from C++ GeometryUtils namespace (geometry_utils.hpp).
"""

import math


def distance(a: tuple[float, float], b: tuple[float, float]) -> float:
    """Euclidean distance between two points."""
    dx = b[0] - a[0]
    dy = b[1] - a[1]
    return math.sqrt(dx * dx + dy * dy)


def angle_rad(a: tuple[float, float], b: tuple[float, float]) -> float:
    """Angle in radians from point a to point b."""
    return math.atan2(b[1] - a[1], b[0] - a[0])


def angle_deg(a: tuple[float, float], b: tuple[float, float]) -> float:
    """Angle in degrees from point a to point b."""
    return math.degrees(angle_rad(a, b))


def normalize_angle(a: tuple[float, float], b: tuple[float, float]) -> float:
    """Normalize angle to [0, 180) range in degrees."""
    deg = angle_deg(a, b)
    if deg < 0:
        deg += 180.0
    return deg
