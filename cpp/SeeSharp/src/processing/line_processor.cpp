#include "processing/line_processor.hpp"
#include "helper/processing_frame_helper.hpp"

#include <algorithm>
#include <cmath>

// Default constructor
LineProcessor::LineProcessor(const LineParams& params) : params_(params) {}

std::pair<cv::Mat, std::vector<BlobMetaData>> LineProcessor::process(cv::Mat& frame)
{
    cv::Mat resultFrame = frame.clone();
    std::vector<BlobMetaData> metadata;

    const int roiX = std::clamp(static_cast<int>(params_.roiX * frame.cols), 0,
                                std::max(0, frame.cols - 1));
    const int roiY = std::clamp(static_cast<int>(params_.roiY * frame.rows), 0,
                                std::max(0, frame.rows - 1));
    const int roiWidth = std::clamp(static_cast<int>(params_.roiWidth * frame.cols),
                                    1, frame.cols - roiX);
    const int roiHeight = std::clamp(static_cast<int>(params_.roiHeight * frame.rows),
                                     1, frame.rows - roiY);
    const cv::Rect roi(roiX, roiY, roiWidth, roiHeight);
    cv::Mat edges;
    cv::Mat gray = ProcessingFrameHelper::getGrayFrame(frame);

    cv::Canny(gray(roi), edges, params_.cannyThreshold1, params_.cannyThreshold2, params_.apertureSize, params_.useL2Gradient);

    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, params_.rho, params_.theta, params_.threshold, params_.minLineLength, params_.maxLineGap);

    int acceptedLines = 0;
    for (const auto& line : lines)
    {
        cv::Point pt1(line[0] + roi.x, line[1] + roi.y);
        cv::Point pt2(line[2] + roi.x, line[3] + roi.y);
        const double angle = std::atan2(pt2.y - pt1.y, pt2.x - pt1.x) *
                             180.0 / CV_PI;
        if (angle < params_.minAngle || angle > params_.maxAngle)
            continue;
        if (acceptedLines++ >= params_.maxLines)
            break;
        cv::line(resultFrame, pt1, pt2, cv::Scalar(255, 255, 255), 1);

        BlobMetaData meta;
        meta.id = 0; // todo
        meta.center = cv::Point2f((pt1.x + pt2.x) / 2.0f, (pt1.y + pt2.y) / 2.0f);
        meta.area = cv::norm(pt1 - pt2);
        meta.boundingBox = cv::boundingRect(std::vector<cv::Point>{pt1, pt2});

        metadata.push_back(meta);
    }

    return { resultFrame, metadata };
}
