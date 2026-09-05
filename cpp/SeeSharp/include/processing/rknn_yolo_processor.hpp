#pragma once

#include <cstdint>
#include <vector>

#include "model/yolo_params.hpp"
#include "processing/i_frame_processor.hpp"

class RknnYoloProcessor : public IFrameProcessor
{
public:
    explicit RknnYoloProcessor(const YoloParams& params);
    ~RknnYoloProcessor() override;

    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;

private:
    bool loadModel();
    std::vector<float> infer(const cv::Mat& rgb);

    YoloParams params_;
    uint64_t context_ = 0;
    uint32_t outputElements_ = 0;
};
