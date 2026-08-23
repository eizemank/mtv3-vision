#pragma once

#include <algorithm>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include "config/config_keys.hpp"
#include "model/blob_detection/blob_params.hpp"
#include "model/circle_params.hpp"
#include "model/general_params.hpp"
#include "model/line_params.hpp"

inline void from_json(const nlohmann::json& j, CriterionParams& p)
{
    j.at("min").get_to(p.min);
    j.at("max").get_to(p.max);
    j.at("goal").get_to(p.goal);
    j.at("weight").get_to(p.weight);
}

inline void from_json(const nlohmann::json& j, NodeSettings& p)
{
    j.at("id").get_to(p.id);
    j.at("blob_id").get_to(p.blobColorPatternIds);
    j.at(ConfigKeys::THRESHOLD).get_to(p.threshold);
    j.at(ConfigKeys::WEIGHT).get_to(p.weight);
    j.at("size").get_to(p.size);
    j.at("circularity").get_to(p.circularity);
    j.at("inertia").get_to(p.inertia);
    j.at("convexity").get_to(p.convexity);
    j.at("angle").get_to(p.angle);
}

inline void from_json(const nlohmann::json& j, LinkSettings& p)
{
    j.at("id").get_to(p.id);
    j.at(ConfigKeys::THRESHOLD).get_to(p.threshold);
    j.at(ConfigKeys::WEIGHT).get_to(p.weight);
    j.at("length_absolute").get_to(p.lengthAbsolute);
    j.at("length_relative").get_to(p.lengthRelative);
    j.at("angle_absolute").get_to(p.angleAbsolute);
    j.at("angle_relative").get_to(p.angleRelative);
}

// TODO (DD): Move strings to constants
inline void from_json(const nlohmann::json& j, OneColorBlobParams& p)
{
    j.at("id").get_to(p.id);
    p.enabled = j.value("enabled", true);
    j.at("min_area").get_to(p.minArea);
    j.at("max_area").get_to(p.maxArea);
    j.at("min_width").get_to(p.minWidth);
    j.at("min_height").get_to(p.minHeight);
    j.at("min_circularity").get_to(p.minCircularity);
    j.at("max_circularity").get_to(p.maxCircularity);
    j.at("min_inertia").get_to(p.minInertia);
    j.at("max_inertia").get_to(p.maxInertia);
    j.at("min_convexity").get_to(p.minConvexity);
    j.at("max_convexity").get_to(p.maxConvexity);
    p.minVertices = j.value("min_vertices", 0);
    p.maxVertices = j.value("max_vertices", 0);
    p.polygonApproximation = j.value("polygon_approximation", 0.02);

    auto lower = j["lower_range"];
    auto upper = j["upper_range"];
    p.lowerRange = cv::Scalar(lower[0], lower[1], lower[2]);
    p.upperRange = cv::Scalar(upper[0], upper[1], upper[2]);
}

inline void from_json(const nlohmann::json& j, MultiColorBlobParams& p)
{
    j.at("id").get_to(p.id);
    j.at("overall_threshold").get_to(p.overallThreshold);

    p.sizeMeasure = sizeMeasureFromString(j.at("size_measure").get<std::string>());

    j.at("nodes").get_to(p.nodes);
    j.at("links").get_to(p.links);
    if (p.nodes.empty() || p.nodes.size() > 5)
        throw std::invalid_argument("A composite object must contain from 1 to 5 primitives");
}

inline void from_json(const nlohmann::json& j, BlobParams& p)
{
    j.at(ConfigKeys::ENABLE_ONE_COLOR_DETECTION).get_to(p.enableOneColorDetection);
    j.at(ConfigKeys::ENABLE_MULTICOLOR_DETECTION).get_to(p.enableMultiColorDetection);
    p.maxCompositeObjects = std::clamp(j.value("max_composite_objects", 5), 1, 5);
    j.at(ConfigKeys::ONE_COLOR_BLOB_PATTERNS_CONFIG_ID).get_to(p.oneColorBlobParams);
    j.at(ConfigKeys::MULTICOLOR_BLOB_PATTERNS_CONFIG_ID).get_to(p.multiColorBlobParams);
}

inline void from_json(const nlohmann::json& j, LineParams& p)
{
    j.at(ConfigKeys::CANNY_THRESHOLD1).get_to(p.cannyThreshold1);
    j.at(ConfigKeys::CANNY_THRESHOLD2).get_to(p.cannyThreshold2);
    j.at(ConfigKeys::CANNY_APERTURE_SIZE).get_to(p.apertureSize);
    j.at(ConfigKeys::CANNY_USE_L2_GRADIENT).get_to(p.useL2Gradient);

    j.at(ConfigKeys::HOUGH_RHO).get_to(p.rho);
    j.at(ConfigKeys::HOUGH_THETA).get_to(p.theta);
    j.at(ConfigKeys::HOUGH_THETA).get_to(p.threshold);
    j.at(ConfigKeys::HOUGH_MIN_LINE_LENGTH).get_to(p.minLineLength);
    j.at(ConfigKeys::HOUGH_MAX_LINE_GAP).get_to(p.maxLineGap);
    p.minAngle = j.value("min_angle", -180.0);
    p.maxAngle = j.value("max_angle", 180.0);
    p.roiX = j.value("roi_x", 0.0);
    p.roiY = j.value("roi_y", 0.0);
    p.roiWidth = j.value("roi_width", 1.0);
    p.roiHeight = j.value("roi_height", 1.0);
    p.maxLines = j.value("max_lines", 100);
}

inline void from_json(const nlohmann::json& j, CircleParams& p)
{
    j.at(ConfigKeys::HOUGH_PARAM1).get_to(p.houghParam1);
    j.at(ConfigKeys::HOUGH_PARAM2).get_to(p.houghParam2);
    j.at(ConfigKeys::MIN_RADIUS).get_to(p.minRadius);
    j.at(ConfigKeys::MAX_RADIUS).get_to(p.maxRadius);
    j.at(ConfigKeys::DISTANCE).get_to(p.distance);
}

inline void from_json(const nlohmann::json& j, GeneralParams& p)
{
    j.at(ConfigKeys::DEBUG_MODE).get_to(p.debugMode);
    p.processingType = processingTypeFromString(j.at(ConfigKeys::PROCESSING_MODE).get<std::string>());
    p.cameraRotation = j.value("camera_rotation", 0);
    p.exposureEv = j.value("exposure_ev", 0.0);
    p.contrast = j.value("contrast", 1.0);
    p.brightness = j.value("brightness", 0.0);
    const auto whiteBalance = j.value(
        "white_balance_bgr", std::vector<double>{1.0, 1.0, 1.0});
    if (whiteBalance.size() != 3)
        throw std::invalid_argument("white_balance_bgr must contain 3 gains");
    p.whiteBalanceBlue = whiteBalance[0];
    p.whiteBalanceGreen = whiteBalance[1];
    p.whiteBalanceRed = whiteBalance[2];
    if (p.cameraRotation != 0 && p.cameraRotation != 90 &&
        p.cameraRotation != 180 && p.cameraRotation != 270)
        throw std::invalid_argument(
            "general_params.camera_rotation must be 0, 90, 180 or 270");
}
