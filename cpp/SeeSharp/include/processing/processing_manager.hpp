#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>
#include <mutex>
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

    /// @brief Горячая смена анализатора: пересоздать процессор по новому
    /// конфигу. Ошибка конфига -> исключение, старый процессор остаётся.
    void reconfigure(const nlohmann::json& config)
    {
        auto p = ProcessingFactory::createProcessor(config);  // вне лока
        std::lock_guard<std::mutex> lock(m_);
        frameProcessor_ = std::move(p);
    }

    /// @brief Process a frame and return the processed frame along with metadata
    std::pair<cv::Mat, std::vector<BlobMetaData>> processFrame(cv::Mat& frame)
    {
        std::lock_guard<std::mutex> lock(m_);
        return frameProcessor_->process(frame);
    }

private:
    std::unique_ptr<IFrameProcessor> frameProcessor_;
    std::mutex m_;
};