#pragma once

#include <cstdint>
#include <vector>

#include "model/blob_meta_data.hpp"
#include "model/common_config.hpp"

struct VisionFrame
{
    uint32_t frameId = 0;
    uint32_t timestampMs = 0;
    uint16_t imageWidth = 0;
    uint16_t imageHeight = 0;
    ProcessingType detectorType = ProcessingType::BlobDetection;
    uint32_t inferenceUs = 0;
    float fps = 0.0f;
    std::vector<BlobMetaData> objects;
};

inline uint8_t detectorTypeCode(ProcessingType type)
{
    switch (type)
    {
        case ProcessingType::Off: return 0x00;
        case ProcessingType::Classification: return 0x01;
        case ProcessingType::ObjectDetection: return 0x01;
        case ProcessingType::ArucoDetection: return 0x02;
        case ProcessingType::BlobDetection: return 0x03;
        case ProcessingType::LineDetection: return 0x04;
        case ProcessingType::CircleDetection: return 0x05;
        default: return 0x00;
    }
}

inline const char* detectorTypeName(ProcessingType type)
{
    switch (type)
    {
        case ProcessingType::Off: return "off";
        case ProcessingType::Classification: return "classification";
        case ProcessingType::ObjectDetection: return "object_detection";
        case ProcessingType::ArucoDetection: return "aruco";
        case ProcessingType::BlobDetection: return "blob";
        case ProcessingType::LineDetection: return "line";
        case ProcessingType::CircleDetection: return "circle";
        default: return "off";
    }
}
