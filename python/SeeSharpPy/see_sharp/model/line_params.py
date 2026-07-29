"""Line detection parameters (Canny + Hough transform)."""

from dataclasses import dataclass

from see_sharp.config import config_keys


@dataclass(frozen=True)
class LineParams:
    """Parameters for Canny edge detection and probabilistic Hough line transform.

    Attributes:
        canny_threshold1: First threshold for Canny hysteresis.
        canny_threshold2: Second threshold for Canny hysteresis.
        aperture_size: Aperture size for the Sobel operator.
        use_l2_gradient: Use L2 norm for gradient magnitude.
        rho: Distance resolution in pixels.
        theta: Angle resolution in radians.
        threshold: Minimum number of votes (intersections).
        min_line_length: Minimum line length to accept.
        max_line_gap: Maximum gap between points on the same line.
    """

    canny_threshold1: float
    canny_threshold2: float
    aperture_size: int
    use_l2_gradient: bool
    rho: float
    theta: float
    threshold: int
    min_line_length: float
    max_line_gap: float

    @classmethod
    def from_config(cls, section: dict) -> "LineParams":
        """Create LineParams from a config dictionary section.

        Note: Fixes C++ bug in json_helper.hpp:91 where HOUGH_THETA was
        incorrectly mapped to the threshold field.
        """
        return cls(
            canny_threshold1=section[config_keys.CANNY_THRESHOLD1],
            canny_threshold2=section[config_keys.CANNY_THRESHOLD2],
            aperture_size=section[config_keys.CANNY_APERTURE_SIZE],
            use_l2_gradient=section[config_keys.CANNY_USE_L2_GRADIENT],
            rho=section[config_keys.HOUGH_RHO],
            theta=section[config_keys.HOUGH_THETA],
            threshold=section[config_keys.HOUGH_THRESHOLD],
            min_line_length=section[config_keys.HOUGH_MIN_LINE_LENGTH],
            max_line_gap=section[config_keys.HOUGH_MAX_LINE_GAP],
        )
