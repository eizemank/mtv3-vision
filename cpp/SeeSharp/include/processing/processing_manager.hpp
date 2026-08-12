#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include "processing/processing_factory.hpp"
#include "model/blob_meta_data.hpp"
#include "model/automapper.hpp"
#include "model/general_params.hpp"

class ProcessingManager
{
public:
    ProcessingManager(const nlohmann::json& config)
    {
        GeneralParams params = Automapper::mapParams<GeneralParams>(
            config, ConfigKeys::GENERAL_PARAMS_CONFIG_ID);
        processingType_ = params.processingType;
        cameraRotation_ = params.cameraRotation;
        frameProcessor_ = ProcessingFactory::createProcessor(config);
    }

    /// @brief Горячая смена анализатора: пересоздать процессор по новому
    /// конфигу. Ошибка конфига -> исключение, старый процессор остаётся.
    void reconfigure(const nlohmann::json& config)
    {
        GeneralParams params = Automapper::mapParams<GeneralParams>(
            config, ConfigKeys::GENERAL_PARAMS_CONFIG_ID);
        auto p = ProcessingFactory::createProcessor(config);  // вне лока
        std::lock_guard<std::mutex> lock(m_);
        frameProcessor_ = std::move(p);
        processingType_ = params.processingType;
        cameraRotation_ = params.cameraRotation;
    }

    /// @brief Process a frame and return the processed frame along with metadata
    std::pair<cv::Mat, std::vector<BlobMetaData>> processFrame(cv::Mat& frame)
    {
        std::lock_guard<std::mutex> lock(m_);
        if (cameraRotation_ == 90)
            cv::rotate(frame, frame, cv::ROTATE_90_CLOCKWISE);
        else if (cameraRotation_ == 180)
            cv::rotate(frame, frame, cv::ROTATE_180);
        else if (cameraRotation_ == 270)
            cv::rotate(frame, frame, cv::ROTATE_90_COUNTERCLOCKWISE);
        return frameProcessor_->process(frame);
    }

    ProcessingType processingType()
    {
        std::lock_guard<std::mutex> lock(m_);
        return processingType_;
    }

private:
    std::unique_ptr<IFrameProcessor> frameProcessor_;
    ProcessingType processingType_ = ProcessingType::BlobDetection;
    int cameraRotation_ = 0;
    std::mutex m_;
};
