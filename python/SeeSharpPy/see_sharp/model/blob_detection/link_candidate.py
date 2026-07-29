"""Link candidate connecting two nodes in multi-color blob detection.

Ported from C++ LinkCandidate (link_candidate.hpp).
Uses factory methods instead of exposing the constructor directly.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from see_sharp.helper.geometry_utils import distance, normalize_angle


@dataclass
class LinkCandidate:
    """A link connecting two node candidates with computed geometric properties.

    Attributes:
        id: String ID in format "nodeA-nodeB" (e.g., "0-1").
        score: Similarity score.
        length: Absolute distance between the two points.
        relative_length: Length relative to base (base_node_size or base_link_length).
        angle: Angle to Ox axis, normalized to [0, 180).
        relative_angle: Angle relative to base (base_node_angle or base_link_angle).
    """

    id: str = ""
    score: float = 0.0
    length: float = 0.0
    relative_length: float = 0.0
    angle: float = 0.0
    relative_angle: float = 0.0

    @classmethod
    def create_base_link(
        cls,
        link_id: str,
        p1: tuple[float, float],
        p2: tuple[float, float],
        base_node_size: float,
        base_node_angle: float,
    ) -> LinkCandidate:
        """Create a base link (first link in the pattern).

        Relative length = length / base_node_size.
        Relative angle = angle - base_node_angle.
        """
        link = cls._create(link_id, p1, p2)
        link.relative_length = link.length / base_node_size if base_node_size != 0 else 0.0
        link.relative_angle = link.angle - base_node_angle
        return link

    @classmethod
    def create_regular_link(
        cls,
        link_id: str,
        p1: tuple[float, float],
        p2: tuple[float, float],
        base_link_length: float,
        base_link_angle: float,
    ) -> LinkCandidate:
        """Create a regular (non-base) link.

        Relative length = length / base_link_length.
        Relative angle = angle - base_link_angle.
        """
        link = cls._create(link_id, p1, p2)
        link.relative_length = link.length / base_link_length if base_link_length != 0 else 0.0
        link.relative_angle = link.angle - base_link_angle
        return link

    @classmethod
    def _create(cls, link_id: str, p1: tuple[float, float], p2: tuple[float, float]) -> LinkCandidate:
        """Internal factory computing length and angle from two points."""
        link = cls()
        link.id = link_id
        link.length = distance(p1, p2)
        link.angle = normalize_angle(p1, p2)
        return link
