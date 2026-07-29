"""Maximum axis of a blob.

Ported from C++ MaxAxis (max_axis.hpp / max_axis.cpp).
"""

from dataclasses import dataclass, field

from see_sharp.helper.geometry_utils import distance, normalize_angle


@dataclass
class MaxAxis:
    """Maximum axis of a blob defined by two endpoints.

    Attributes:
        start: Start point (x, y).
        end: End point (x, y).
        length: Euclidean distance between start and end.
        angle: Angle in degrees [0, 180) to the horizontal axis.
    """

    start: tuple[float, float] = (0.0, 0.0)
    end: tuple[float, float] = (0.0, 0.0)
    length: float = field(init=False, default=0.0)
    angle: float = field(init=False, default=0.0)

    def __post_init__(self) -> None:
        self.length = distance(self.start, self.end)
        self.angle = normalize_angle(self.start, self.end)
