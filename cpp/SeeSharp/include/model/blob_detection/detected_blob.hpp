#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include "model/max_axis.hpp"

/// @brief Struct to hold blob properties, most of them are calculated
struct DetectedBlob
{
public:
    // Color property
    int colorPatternId;

    // Morphological properties
    double circularity;
    double inertia;
    double convexity;

    // Size properties

    // Maximum axis of the blob, note that it contains blob angle
    MaxAxis maxAxis;
    double area;
    double hullArea;
    cv::Rect boundingBox;

    // Common
    std::vector<cv::Point> contour;
    std::vector<cv::Point> hullContour;
    cv::Point2f center;

    // Default constructor
    DetectedBlob() : colorPatternId(-1), area(0.0), hullArea(0.0), circularity(0.0), inertia(0.0), convexity(0.0), center(0, 0)
    {
        maxAxis = MaxAxis();
    }

    DetectedBlob(const std::vector<cv::Point>& contour, int colorPatternId);

private:
    double calculateInertia() const;
    double calculateConvexity() const;
    MaxAxis getMaxAxis() const;
};