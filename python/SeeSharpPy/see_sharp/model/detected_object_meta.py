"""Generic detected object metadata.

ISP improvement: Renamed from C++ BlobMetaData to DetectedObjectMeta.
This is a universal output structure used by all processors,
not just blob detection.
"""

from dataclasses import dataclass, field


@dataclass
class DetectedObjectMeta:
    """Metadata describing a single detected object in a frame.

    Attributes:
        id: Identifier for the detected object.
        center_x: X coordinate of the object's center.
        center_y: Y coordinate of the object's center.
        area: Area or length measure of the detected object.
        bbox_x: Bounding box top-left X.
        bbox_y: Bounding box top-left Y.
        bbox_w: Bounding box width.
        bbox_h: Bounding box height.
    """

    id: int = 0
    center_x: float = 0.0
    center_y: float = 0.0
    area: float = 0.0
    bbox_x: int = 0
    bbox_y: int = 0
    bbox_w: int = 0
    bbox_h: int = 0
