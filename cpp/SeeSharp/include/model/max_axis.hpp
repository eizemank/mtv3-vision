#pragma once

#include <opencv2/opencv.hpp>

/// @brief Struct to hold the maximum axis of a blob
struct MaxAxis
{
public:
    // Actually it doesn't matter which point is start and which is end
    cv::Point2f start;
    cv::Point2f end;
    double length;

    // Angle in degrees 0-180 to the horizontal axis
    double angle;

    MaxAxis() : start(0, 0), end(0, 0), length(0.0) {}
    MaxAxis(const cv::Point2f& s, const cv::Point2f& e);
};