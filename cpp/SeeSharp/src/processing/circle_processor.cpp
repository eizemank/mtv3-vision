#include "processing/circle_processor.hpp"
#include "helper/processing_frame_helper.hpp"

static int trackMinRadius = 10;
static int trackMaxRadius = 50;
static int trackParam1 = 100;
static int trackParam2 = 30;
static int trackDistance = 6;
static int gaussianBlurSize = 9; // todo

constexpr const char* RESULT_WINDOW_NAME = "Result";

// Default constructor
CircleProcessor::CircleProcessor(const CircleParams& circleParams) : circleParams_(circleParams) { }

std::pair<cv::Mat, std::vector<BlobMetaData>> CircleProcessor::process(cv::Mat& frame)
{
    cv::Mat resultFrame = frame.clone();
    std::vector<BlobMetaData> metadata;

    cv::Mat edges, blurred;
    cv::Mat gray = ProcessingFrameHelper::getGrayFrame(frame);
    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2);

    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(blurred, circles, cv::HOUGH_GRADIENT, 1,
        circleParams_.distance,
        circleParams_.houghParam1, circleParams_.houghParam2,
        circleParams_.minRadius, circleParams_.maxRadius);

    for (const auto& circle : circles)
    {
        cv::Point center(cvRound(circle[0]), cvRound(circle[1]));
        int radius = cvRound(circle[2]);
        cv::circle(resultFrame, center, radius, cv::Scalar(0, 255, 0), 2);
        cv::circle(resultFrame, center, 2, cv::Scalar(0, 0, 255), 3);

        // collect metadata
    }

    return { resultFrame, metadata };
}