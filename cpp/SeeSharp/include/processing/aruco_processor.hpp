#pragma once

#include <opencv2/opencv.hpp>
#include "processing/i_frame_processor.hpp"
#include "model/blob_meta_data.hpp"

class ArucoProcessor : public IFrameProcessor
{
public:
    ArucoProcessor() {}
    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;
};