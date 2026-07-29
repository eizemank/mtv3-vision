#pragma once

#include <opencv2/opencv.hpp>
#include "model/line_params.hpp"
#include "processing/i_frame_processor.hpp"
#include "model/blob_meta_data.hpp"

class LineProcessor : public IFrameProcessor
{
public:
    LineProcessor(const LineParams& params);

    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;
private:
    LineParams params_;
};