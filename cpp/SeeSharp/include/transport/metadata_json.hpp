#pragma once

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "transport/vision_frame.hpp"

inline nlohmann::json serializeVisionFrameJson(const VisionFrame& frame,
                                               size_t maxObjects = 200)
{
    nlohmann::json detections = nlohmann::json::array();
    nlohmann::json arucoMarkers = nlohmann::json::array();
    nlohmann::json lines = nlohmann::json::array();
    nlohmann::json circles = nlohmann::json::array();
    nlohmann::json blobs = nlohmann::json::array();
    const size_t count = std::min(frame.objects.size(), maxObjects);
    const double width = std::max<uint16_t>(1, frame.imageWidth);
    const double height = std::max<uint16_t>(1, frame.imageHeight);

    for (size_t index = 0; index < count; ++index)
    {
        const BlobMetaData& object = frame.objects[index];
        const nlohmann::json center = {
            {"x", object.center.x}, {"y", object.center.y}};
        const nlohmann::json normalizedBox = {
            {"x", object.center.x / width}, {"y", object.center.y / height},
            {"w", object.boundingBox.width / width},
            {"h", object.boundingBox.height / height}};

        switch (frame.detectorType)
        {
            case ProcessingType::Classification:
            case ProcessingType::ObjectDetection:
                detections.push_back({
                    {"id", index}, {"class_id", object.id},
                    {"class_name", "class_" + std::to_string(object.id)},
                    {"confidence", std::clamp(object.confidence, 0.0, 1.0)},
                    {"bbox", normalizedBox}, {"timestamp_ms", frame.timestampMs}});
                break;
            case ProcessingType::ArucoDetection:
            {
                nlohmann::json corners = nlohmann::json::array();
                for (const cv::Point2f& point : object.points)
                    corners.push_back({{"x", point.x}, {"y", point.y}});
                arucoMarkers.push_back({
                    {"marker_id", object.id},
                    {"corners", std::move(corners)},
                    {"center", center}, {"pose_rvec", {0.0, 0.0, 0.0}},
                    {"pose_tvec", {0.0, 0.0, 0.0}}});
                break;
            }
            case ProcessingType::LineDetection:
            {
                if (object.points.size() < 2)
                    break;
                const double x1 = object.points[0].x;
                const double y1 = object.points[0].y;
                const double x2 = object.points[1].x;
                const double y2 = object.points[1].y;
                lines.push_back({{"x1", x1}, {"y1", y1}, {"x2", x2}, {"y2", y2},
                                 {"angle_deg", std::atan2(y2 - y1, x2 - x1) * 180.0 / CV_PI},
                                 {"length", object.area}});
                break;
            }
            case ProcessingType::CircleDetection:
                circles.push_back({{"center", center},
                                   {"radius", object.radius}});
                break;
            case ProcessingType::BlobDetection:
                blobs.push_back({{"id", object.id}, {"center", center},
                                 {"area", object.area},
                                 {"circularity", object.circularity}});
                break;
            default:
                break;
        }
    }

    return {
        {"version", "1.0"}, {"msg_type", "detection_frame"},
        {"frame_id", frame.frameId}, {"timestamp_ms", frame.timestampMs},
        {"image_size", {frame.imageWidth, frame.imageHeight}},
        {"detector", detectorTypeName(frame.detectorType)},
        {"inference_ms", frame.inferenceUs / 1000.0}, {"fps", frame.fps},
        {"detections", std::move(detections)},
        {"aruco_markers", std::move(arucoMarkers)},
        {"lines", std::move(lines)}, {"circles", std::move(circles)},
        {"blobs", std::move(blobs)}
    };
}
