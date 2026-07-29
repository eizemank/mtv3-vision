"""General application parameters and processing type enum."""

from dataclasses import dataclass
from enum import Enum

from see_sharp.config import config_keys


class ProcessingType(Enum):
    """Available processing algorithms.

    Values match the JSON config strings so conversion is trivial:
    ProcessingType("line_detection") works directly.
    """

    BLOB_DETECTION = config_keys.BLOB_DETECTION
    LINE_DETECTION = config_keys.LINE_DETECTION
    CIRCLE_DETECTION = config_keys.CIRCLE_DETECTION
    ARUCO_DETECTION = config_keys.ARUCO_DETECTION


@dataclass(frozen=True)
class GeneralParams:
    """General application configuration.

    Attributes:
        debug_mode: Enable debug visualization and logging.
        processing_type: Which processing algorithm to use.
    """

    debug_mode: bool
    processing_type: ProcessingType

    @classmethod
    def from_config(cls, section: dict) -> "GeneralParams":
        """Create GeneralParams from a config dictionary section."""
        return cls(
            debug_mode=section[config_keys.DEBUG_MODE],
            processing_type=ProcessingType(section[config_keys.PROCESSING_MODE]),
        )
