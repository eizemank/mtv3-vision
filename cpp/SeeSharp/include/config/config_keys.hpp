#pragma once

namespace ConfigKeys
{
    constexpr const char* GENERAL_PARAMS_CONFIG_ID = "general_params";
    constexpr const char* BLOB_DETECTION_CONFIG_ID = "blob_detection";
    constexpr const char* LINE_DETECTION_CONFIG_ID = "line_detection";
    constexpr const char* CIRCLE_DETECTION_CONFIG_ID = "circle_detection";
    constexpr const char* ARUCO_DETECTION_CONFIG_ID = "aruco_detection";
    constexpr const char* CLASSIFICATION_CONFIG_ID = "classification";


    constexpr const char* ONE_COLOR_BLOB_PATTERNS_CONFIG_ID = "one_color_patterns";
    constexpr const char* MULTICOLOR_BLOB_PATTERNS_CONFIG_ID = "multicolor_patterns";
    constexpr const char* ENABLE_ONE_COLOR_DETECTION = "enable_one_color_detection";
    constexpr const char* ENABLE_MULTICOLOR_DETECTION = "enable_multicolor_detection";

    constexpr const char* CANNY_THRESHOLD1 = "canny_threshold1";
    constexpr const char* CANNY_THRESHOLD2 = "canny_threshold2";
    constexpr const char* CANNY_APERTURE_SIZE = "canny_aperture_size";
    constexpr const char* CANNY_USE_L2_GRADIENT = "canny_l2_gradient";
    constexpr const char* HOUGH_RHO = "hough_rho";
    constexpr const char* HOUGH_THETA = "hough_theta";
    constexpr const char* HOUGH_THRESHOLD = "hough_threshold";
    constexpr const char* HOUGH_MIN_LINE_LENGTH = "hough_min_line_length";
    constexpr const char* HOUGH_MAX_LINE_GAP = "hough_max_line_gap";
    constexpr const char* HOUGH_PARAM1 = "hough_param1";
    constexpr const char* HOUGH_PARAM2 = "hough_param2";


    constexpr const char* WEIGHT = "weight";
    constexpr const char* THRESHOLD = "threshold";

    constexpr const char* MIN_RADIUS = "min_radius";
    constexpr const char* MAX_RADIUS = "max_radius";
    constexpr const char* DISTANCE = "distance";

    constexpr const char* DEBUG_MODE = "debug_mode";
    constexpr const char* PROCESSING_MODE = "processing_mode";
}