#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

// TODO (DD): Need to be more abstract and rename this struct to something like DetectedObjectInfo
// We most likely will reuse it for another object, not only blobs.
struct BlobMetaData
{
    int id = 0;
    cv::Point2f center;
    double area = 0.0;
    double confidence = 1.0;
    double circularity = 0.0;
    double radius = 0.0;
    cv::Rect boundingBox;
    std::vector<cv::Point2f> points;
};
