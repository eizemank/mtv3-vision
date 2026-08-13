#include "processing/aruco_processor.hpp"
#include <algorithm>
#include <opencv2/aruco.hpp>

namespace
{
cv::aruco::PREDEFINED_DICTIONARY_NAME dictionaryByName(const std::string& name)
{
    if (name == "DICT_4X4_100") return cv::aruco::DICT_4X4_100;
    if (name == "DICT_4X4_250") return cv::aruco::DICT_4X4_250;
    if (name == "DICT_4X4_1000") return cv::aruco::DICT_4X4_1000;
    if (name == "DICT_5X5_50") return cv::aruco::DICT_5X5_50;
    if (name == "DICT_5X5_100") return cv::aruco::DICT_5X5_100;
    if (name == "DICT_5X5_250") return cv::aruco::DICT_5X5_250;
    if (name == "DICT_5X5_1000") return cv::aruco::DICT_5X5_1000;
    if (name == "DICT_6X6_50") return cv::aruco::DICT_6X6_50;
    if (name == "DICT_6X6_100") return cv::aruco::DICT_6X6_100;
    if (name == "DICT_6X6_250") return cv::aruco::DICT_6X6_250;
    if (name == "DICT_6X6_1000") return cv::aruco::DICT_6X6_1000;
    if (name == "DICT_7X7_50") return cv::aruco::DICT_7X7_50;
    if (name == "DICT_7X7_100") return cv::aruco::DICT_7X7_100;
    if (name == "DICT_7X7_250") return cv::aruco::DICT_7X7_250;
    if (name == "DICT_7X7_1000") return cv::aruco::DICT_7X7_1000;
    if (name == "DICT_ARUCO_ORIGINAL") return cv::aruco::DICT_ARUCO_ORIGINAL;
    return cv::aruco::DICT_4X4_50;
}
}

std::pair<cv::Mat, std::vector<BlobMetaData>> ArucoProcessor::process(cv::Mat& frame)
{
    cv::Mat resultFrame = frame.clone();
    std::vector<BlobMetaData> metadata;

#if CV_VERSION_MAJOR > 4 || (CV_VERSION_MAJOR == 4 && CV_VERSION_MINOR >= 6)
    cv::Ptr<cv::aruco::Dictionary> dictionary =
        cv::makePtr<cv::aruco::Dictionary>(
            cv::aruco::getPredefinedDictionary(dictionaryByName(params_.dictionary)));
#else
    cv::Ptr<cv::aruco::Dictionary> dictionary =
        cv::aruco::getPredefinedDictionary(dictionaryByName(params_.dictionary));
#endif

    std::vector<int> markerIds;
    std::vector<std::vector<cv::Point2f>> markerCorners;

    // Распознавание маркеров
    cv::aruco::detectMarkers(frame, dictionary, markerCorners, markerIds);

    // Визуализация + метаданные (как в python-версии)
    if (!markerIds.empty()) {
        cv::aruco::drawDetectedMarkers(resultFrame, markerCorners, markerIds);

        for (size_t i = 0; i < markerIds.size(); ++i) {
            if (!params_.allowedIds.empty() &&
                std::find(params_.allowedIds.begin(), params_.allowedIds.end(),
                          markerIds[i]) == params_.allowedIds.end())
                continue;
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
            if (meta.area < params_.minArea || meta.area > params_.maxArea)
                continue;
            meta.boundingBox = cv::boundingRect(ipts);
            metadata.push_back(meta);
        }
    }

    return { resultFrame, metadata };
}
