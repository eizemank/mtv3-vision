#pragma once

#include "processing/i_frame_processor.hpp"

class PassthroughProcessor : public IFrameProcessor
{
public:
    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override
    {
        return {frame.clone(), {}};
    }
};
