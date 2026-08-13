#pragma once

#include <opencv2/opencv.hpp>
#include "processing/i_frame_processor.hpp"
#include "model/blob_meta_data.hpp"
#include "model/aruco_params.hpp"

class ArucoProcessor : public IFrameProcessor
{
public:
    explicit ArucoProcessor(const ArucoParams& params) : params_(params) {}
    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;
private:
    ArucoParams params_;
};
