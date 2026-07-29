#include "processing/line_processor.hpp"
#include "helper/processing_frame_helper.hpp"

// Default constructor
LineProcessor::LineProcessor(const LineParams& params) : params_(params) {}

std::pair<cv::Mat, std::vector<BlobMetaData>> LineProcessor::process(cv::Mat& frame)
{
    cv::Mat resultFrame = frame.clone();
    std::vector<BlobMetaData> metadata;

    cv::Mat edges;
    cv::Mat gray = ProcessingFrameHelper::getGrayFrame(frame);

    cv::Canny(gray, edges, params_.cannyThreshold1, params_.cannyThreshold2, params_.apertureSize, params_.useL2Gradient);

    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, params_.rho, params_.theta, params_.threshold, params_.minLineLength, params_.maxLineGap);

    for (const auto& line : lines)
    {
        cv::Point pt1(line[0], line[1]);
        cv::Point pt2(line[2], line[3]);
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