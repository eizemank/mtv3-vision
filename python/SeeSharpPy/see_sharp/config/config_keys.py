"""String constants for JSON configuration keys.

Maps directly from C++ ConfigKeys namespace (config_keys.hpp).
"""

# Top-level config section IDs
GENERAL_PARAMS = "general_params"
BLOB_DETECTION = "blob_detection"
LINE_DETECTION = "line_detection"
CIRCLE_DETECTION = "circle_detection"
ARUCO_DETECTION = "aruco_detection"

# Blob detection keys
ONE_COLOR_PATTERNS = "one_color_patterns"
MULTICOLOR_PATTERNS = "multicolor_patterns"
ENABLE_ONE_COLOR_DETECTION = "enable_one_color_detection"
ENABLE_MULTICOLOR_DETECTION = "enable_multicolor_detection"

# Canny edge detection keys
CANNY_THRESHOLD1 = "canny_threshold1"
CANNY_THRESHOLD2 = "canny_threshold2"
CANNY_APERTURE_SIZE = "canny_aperture_size"
CANNY_USE_L2_GRADIENT = "canny_l2_gradient"

# Hough transform keys
HOUGH_RHO = "hough_rho"
HOUGH_THETA = "hough_theta"
HOUGH_THRESHOLD = "hough_threshold"
HOUGH_MIN_LINE_LENGTH = "hough_min_line_length"
HOUGH_MAX_LINE_GAP = "hough_max_line_gap"
HOUGH_PARAM1 = "hough_param1"
HOUGH_PARAM2 = "hough_param2"

# Shared keys
WEIGHT = "weight"
THRESHOLD = "threshold"

# Circle detection keys
MIN_RADIUS = "min_radius"
MAX_RADIUS = "max_radius"
DISTANCE = "distance"

# General params keys
DEBUG_MODE = "debug_mode"
PROCESSING_MODE = "processing_mode"
