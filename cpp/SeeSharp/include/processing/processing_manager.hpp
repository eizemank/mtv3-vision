#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "processing/processing_factory.hpp"
#include "model/blob_meta_data.hpp"

class ProcessingManager
{
public:
    ProcessingManager(const nlohmann::json& config)
    {
        frameProcessor_ = ProcessingFactory::createProcessor(config);
    }

    /// @brief Process a frame and return the processed frame along with metadata
    std::pair<cv::Mat, std::vector<BlobMetaData>> processFrame(cv::Mat& frame)
    {
        return frameProcessor_->process(frame);
    }

private:
    std::unique_ptr<IFrameProcessor> frameProcessor_;
};