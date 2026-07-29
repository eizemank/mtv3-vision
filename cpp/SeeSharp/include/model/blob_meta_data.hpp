#pragma once
#include <opencv2/opencv.hpp>

// TODO (DD): Need to be more abstract and rename this struct to something like DetectedObjectInfo
// We most likely will reuse it for another object, not only blobs.
struct BlobMetaData
{
    int id;
    cv::Point2f center;
    double area;
    cv::Rect boundingBox;
};
