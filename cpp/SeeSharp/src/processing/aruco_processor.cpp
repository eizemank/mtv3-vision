#include "processing/aruco_processor.hpp"
#include <opencv2/aruco.hpp>

std::pair<cv::Mat, std::vector<BlobMetaData>> ArucoProcessor::process(cv::Mat& frame)
{
    cv::Mat resultFrame = frame.clone();
    std::vector<BlobMetaData> metadata;

    cv::Ptr<cv::aruco::Dictionary> dictionary =
        cv::aruco::getPredefinedDictionary(cv::aruco::DICT_4X4_50);

    std::vector<int> markerIds;
    std::vector<std::vector<cv::Point2f>> markerCorners;

    // Распознавание маркеров
    cv::aruco::detectMarkers(frame, dictionary, markerCorners, markerIds);

    // Визуализация
    if (!markerIds.empty()) {
        cv::aruco::drawDetectedMarkers(resultFrame, markerCorners, markerIds);
    }

    return { resultFrame, metadata };
}