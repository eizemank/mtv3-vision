"""Circle detection parameters (Hough circle transform)."""

from dataclasses import dataclass

from see_sharp.config import config_keys


@dataclass(frozen=True)
class CircleParams:
    """Parameters for Hough circle transform.

    Attributes:
        min_radius: Minimum circle radius to detect.
        max_radius: Maximum circle radius to detect.
        hough_param1: Higher Canny edge threshold.
        hough_param2: Accumulator threshold for circle centers.
        distance: Minimum distance between detected circle centers.
    """

    min_radius: float
    max_radius: float
    hough_param1: float
    hough_param2: float
    distance: int

    @classmethod
    def from_config(cls, section: dict) -> "CircleParams":
        """Create CircleParams from a config dictionary section."""
        return cls(
            min_radius=section[config_keys.MIN_RADIUS],
            max_radius=section[config_keys.MAX_RADIUS],
            hough_param1=section[config_keys.HOUGH_PARAM1],
            hough_param2=section[config_keys.HOUGH_PARAM2],
            distance=section[config_keys.DISTANCE],
        )
