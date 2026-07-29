"""Blob detection configuration parameters.

Ported from C++ blob_params.hpp. Contains all parameter structures
for single-color and multi-color blob detection.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from enum import Enum

from see_sharp.config import config_keys


class SizeMeasure(Enum):
    """Defines which size metric to use for blob comparison."""

    MAX_AXIS = "max_axis"
    WIDTH = "width"
    HEIGHT = "height"
    AREA = "area"
    CONVEX_AREA = "convex_area"


@dataclass(frozen=True)
class CriterionParams:
    """Criterion with min/max/goal range and weight for scoring.

    Used in multi-color blob detection to score how well a detected
    property matches the expected goal value.
    """

    min: float
    max: float
    goal: float
    weight: int

    def get_weighted_score(self, value: float) -> float:
        """Calculate weighted score for a value within [min, max].

        Returns 0 if out of range, otherwise (1 - normalized_distance) * weight.
        """
        if value < self.min or value > self.max:
            return 0.0

        range_size = abs(self.max - self.min)
        score = 1.0 - (abs(value - self.goal) / range_size)
        return score * self.weight

    def is_in_range(self, value: float) -> bool:
        """Check if value is within [min, max]."""
        return self.min <= value <= self.max

    @classmethod
    def from_config(cls, section: dict) -> CriterionParams:
        return cls(
            min=section["min"],
            max=section["max"],
            goal=section["goal"],
            weight=section["weight"],
        )


@dataclass(frozen=True)
class OneColorBlobParams:
    """Parameters for detecting a single color blob.

    Defines color range in YCrCb space, area bounds,
    minimum size, and morphological property ranges.
    """

    id: int
    min_area: float
    max_area: float
    min_width: int
    min_height: int
    lower_range: tuple[int, int, int]
    upper_range: tuple[int, int, int]
    min_circularity: float
    max_circularity: float
    min_inertia: float
    max_inertia: float
    min_convexity: float
    max_convexity: float

    @classmethod
    def from_config(cls, section: dict) -> OneColorBlobParams:
        lower = section["lower_range"]
        upper = section["upper_range"]
        return cls(
            id=section["id"],
            min_area=section["min_area"],
            max_area=section["max_area"],
            min_width=section["min_width"],
            min_height=section["min_height"],
            lower_range=(lower[0], lower[1], lower[2]),
            upper_range=(upper[0], upper[1], upper[2]),
            min_circularity=section["min_circularity"],
            max_circularity=section["max_circularity"],
            min_inertia=section["min_inertia"],
            max_inertia=section["max_inertia"],
            min_convexity=section["min_convexity"],
            max_convexity=section["max_convexity"],
        )


@dataclass(frozen=True)
class NodeSettings:
    """Settings for a node in a multi-color blob pattern.

    IMPORTANT: Base node must have id = 0.
    """

    id: int
    blob_color_pattern_ids: list[int]
    threshold: float
    weight: int
    size: CriterionParams
    circularity: CriterionParams
    inertia: CriterionParams
    convexity: CriterionParams
    angle: CriterionParams

    def is_base_node(self) -> bool:
        return self.id == 0

    def get_weighted_score(self, value: float) -> float:
        return value * self.weight

    @classmethod
    def from_config(cls, section: dict) -> NodeSettings:
        return cls(
            id=section["id"],
            blob_color_pattern_ids=section["blob_id"],
            threshold=section[config_keys.THRESHOLD],
            weight=section[config_keys.WEIGHT],
            size=CriterionParams.from_config(section["size"]),
            circularity=CriterionParams.from_config(section["circularity"]),
            inertia=CriterionParams.from_config(section["inertia"]),
            convexity=CriterionParams.from_config(section["convexity"]),
            angle=CriterionParams.from_config(section["angle"]),
        )


@dataclass(frozen=True)
class LinkSettings:
    """Settings for a link connecting two nodes in a multi-color pattern.

    The id field contains node IDs separated by a dash (e.g., "0-1").
    """

    id: str
    threshold: float
    weight: int
    length_absolute: CriterionParams
    length_relative: CriterionParams
    angle_absolute: CriterionParams
    angle_relative: CriterionParams

    def is_base_link(self) -> bool:
        return self.id[0] == "0" and self.id[2] == "1"

    def get_weighted_score(self, value: float) -> float:
        return value * self.weight

    def get_first_node_id(self) -> int:
        pos = self.id.index("-")
        return int(self.id[:pos])

    def get_second_node_id(self) -> int:
        pos = self.id.index("-")
        return int(self.id[pos + 1 :])

    @classmethod
    def from_config(cls, section: dict) -> LinkSettings:
        return cls(
            id=section["id"],
            threshold=section[config_keys.THRESHOLD],
            weight=section[config_keys.WEIGHT],
            length_absolute=CriterionParams.from_config(section["length_absolute"]),
            length_relative=CriterionParams.from_config(section["length_relative"]),
            angle_absolute=CriterionParams.from_config(section["angle_absolute"]),
            angle_relative=CriterionParams.from_config(section["angle_relative"]),
        )


@dataclass(frozen=True)
class MultiColorBlobParams:
    """Parameters for a multi-color blob pattern (graph of nodes + links)."""

    id: int
    overall_threshold: float
    size_measure: SizeMeasure
    nodes: list[NodeSettings]
    links: list[LinkSettings]

    @classmethod
    def from_config(cls, section: dict) -> MultiColorBlobParams:
        return cls(
            id=section["id"],
            overall_threshold=section["overall_threshold"],
            size_measure=SizeMeasure(section["size_measure"]),
            nodes=[NodeSettings.from_config(n) for n in section["nodes"]],
            links=[LinkSettings.from_config(l) for l in section["links"]],
        )


@dataclass(frozen=True)
class BlobParams:
    """Top-level blob detection parameters."""

    enable_one_color_detection: bool
    enable_multicolor_detection: bool
    one_color_blob_params: list[OneColorBlobParams]
    multicolor_blob_params: list[MultiColorBlobParams]

    @classmethod
    def from_config(cls, section: dict) -> BlobParams:
        return cls(
            enable_one_color_detection=section[config_keys.ENABLE_ONE_COLOR_DETECTION],
            enable_multicolor_detection=section[config_keys.ENABLE_MULTICOLOR_DETECTION],
            one_color_blob_params=[
                OneColorBlobParams.from_config(p)
                for p in section[config_keys.ONE_COLOR_PATTERNS]
            ],
            multicolor_blob_params=[
                MultiColorBlobParams.from_config(p)
                for p in section[config_keys.MULTICOLOR_PATTERNS]
            ],
        )
