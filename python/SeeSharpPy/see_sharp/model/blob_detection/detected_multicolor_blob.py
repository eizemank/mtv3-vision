"""Detected multi-color blob (a matched pattern of multiple single-color blobs).

Ported from C++ DetectedMulticolorBlob (detected_multicolor_blob.hpp).
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from see_sharp.model.blob_detection.link_candidate import LinkCandidate
from see_sharp.model.blob_detection.node_candidate import NodeCandidate


@dataclass
class DetectedMulticolorBlob:
    """A matched multi-color blob pattern.

    Attributes:
        similarity: Overall similarity score.
        contour: Merged convex hull contour of all nodes.
        node_candidates: Node candidates that form this pattern.
        link_candidates: Link candidates connecting the nodes.
    """

    similarity: float = 0.0
    contour: np.ndarray | None = None
    node_candidates: list[NodeCandidate] = field(default_factory=list)
    link_candidates: list[LinkCandidate] = field(default_factory=list)
