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

    // Визуализация + метаданные (как в python-версии)
    if (!markerIds.empty()) {
        cv::aruco::drawDetectedMarkers(resultFrame, markerCorners, markerIds);

        for (size_t i = 0; i < markerIds.size(); ++i) {
            const auto& pts = markerCorners[i];   // 4 угла маркера

            cv::Point2f center(0, 0);
            for (const auto& p : pts)
                center += p;
            center *= 0.25f;

            std::vector<cv::Point> ipts;
            for (const auto& p : pts)
                ipts.emplace_back(cvRound(p.x), cvRound(p.y));

            BlobMetaData meta;
            meta.id = markerIds[i];
            meta.center = center;
            meta.area = cv::contourArea(ipts);
            meta.boundingBox = cv::boundingRect(ipts);
            metadata.push_back(meta);
        }
    }

    return { resultFrame, metadata };
}