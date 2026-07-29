#pragma once
#include <opencv2/opencv.hpp>
#include <vector>
#include "model/blob_meta_data.hpp"

class IFrameProcessor
{
public:
    virtual std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) = 0; // TODO (DD): = 0 ?
    virtual ~IFrameProcessor() = default;
};
