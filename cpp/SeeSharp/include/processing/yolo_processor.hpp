#pragma once

#include <opencv2/dnn.hpp>

#include "model/yolo_params.hpp"
#include "processing/i_frame_processor.hpp"

class YoloProcessor : public IFrameProcessor
{
public:
    explicit YoloProcessor(const YoloParams& params);
    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;

private:
    YoloParams params_;
    cv::dnn::Net net_;
};
