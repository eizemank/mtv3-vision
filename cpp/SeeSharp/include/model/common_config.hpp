#pragma once

#include "config/config_keys.hpp"

/// @brief Enum to define algorithm to be used via processing video stream
enum class ProcessingType
{
    Off,
    BlobDetection,
    LineDetection,
    CircleDetection,
    ArucoDetection,
    Classification,
    Calibration,
};

inline ProcessingType processingTypeFromString(const std::string& str)
{
    if (str == "off") return ProcessingType::Off;
    if (str == ConfigKeys::BLOB_DETECTION_CONFIG_ID) return ProcessingType::BlobDetection;
    if (str == ConfigKeys::LINE_DETECTION_CONFIG_ID) return ProcessingType::LineDetection;
    if (str == ConfigKeys::CIRCLE_DETECTION_CONFIG_ID) return ProcessingType::CircleDetection;
    if (str == ConfigKeys::ARUCO_DETECTION_CONFIG_ID) return ProcessingType::ArucoDetection;
    if (str == ConfigKeys::CLASSIFICATION_CONFIG_ID) return ProcessingType::Classification;
    throw std::invalid_argument("Unknown ProcessingType: " + str);
}
