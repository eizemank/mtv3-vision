#pragma once

#include <opencv2/opencv.hpp>
#include "model/circle_params.hpp"
#include "processing/i_frame_processor.hpp"
#include "model/blob_meta_data.hpp"

class CircleProcessor : public IFrameProcessor
{
public:
    CircleProcessor(const CircleParams& circleParams);
    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;

private:
    CircleParams circleParams_;
};