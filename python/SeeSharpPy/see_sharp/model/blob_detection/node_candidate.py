"""Node candidate for multi-color blob detection.

Ported from C++ NodeCandidate (node_candidate.hpp).
"""

from __future__ import annotations

from dataclasses import dataclass, field

from see_sharp.model.blob_detection.detected_blob import DetectedBlob


@dataclass
class NodeCandidate:
    """A candidate blob matching a specific node in a multi-color pattern.

    Attributes:
        node_id: ID of the node settings this candidate matches.
        detected_blob: The actual detected blob.
        score: Similarity score computed during matching.
        size: Computed size value (absolute for base, relative for others).
        angle: Angle of the blob in degrees.
    """

    node_id: int = -1
    detected_blob: DetectedBlob | None = None
    score: float = 0.0
    size: float = 0.0
    angle: float = 0.0
