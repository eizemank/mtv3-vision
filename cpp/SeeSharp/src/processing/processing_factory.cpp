#include "processing/processing_factory.hpp"

#include <algorithm>
#include "model/common_config.hpp"
#include "model/blob_detection/blob_params.hpp"
#include "processing/blob_processor.hpp"
#include "processing/line_processor.hpp"
#include "processing/circle_processor.hpp"
#include "model/automapper.hpp"
#include "config/config_keys.hpp"
#include "model/general_params.hpp"
#include "processing/aruco_processor.hpp"
#include "model/yolo_params.hpp"
#include "model/classifier_params.hpp"
#include "processing/classifier_processor.hpp"
#ifdef MTV3_BOARD
#include "processing/rknn_yolo_processor.hpp"
#else
#include "processing/yolo_processor.hpp"
#endif
#include "processing/passthrough_processor.hpp"
#include "model/aruco_params.hpp"

std::unique_ptr<IFrameProcessor> ProcessingFactory::createProcessor(const nlohmann::json& rawConfig)
{
    // Get general params
    GeneralParams generalParams = Automapper::mapParams<GeneralParams>(rawConfig, ConfigKeys::GENERAL_PARAMS_CONFIG_ID);
    switch (generalParams.processingType)
    {
        case ProcessingType::Off:
            return std::make_unique<PassthroughProcessor>();
        case ProcessingType::BlobDetection:
        {
            BlobParams params = Automapper::mapParams<BlobParams>(rawConfig, ConfigKeys::BLOB_DETECTION_CONFIG_ID);
            return std::make_unique<BlobProcessor>(params);
        }

        case ProcessingType::LineDetection:
        {
            LineParams lineParams = Automapper::mapParams<LineParams>(rawConfig, ConfigKeys::LINE_DETECTION_CONFIG_ID);
            return std::make_unique<LineProcessor>(lineParams);
        }

        case ProcessingType::CircleDetection:
        {
            CircleParams circleParams = Automapper::mapParams<CircleParams>(rawConfig, ConfigKeys::CIRCLE_DETECTION_CONFIG_ID);
            return std::make_unique<CircleProcessor>(circleParams);
        }
        case ProcessingType::ArucoDetection:
        {
            ArucoParams params = Automapper::mapParams<ArucoParams>(
                rawConfig, ConfigKeys::ARUCO_DETECTION_CONFIG_ID);
            return std::make_unique<ArucoProcessor>(params);
        }
        case ProcessingType::Classification:
        {
            ClassifierParams params = Automapper::mapParams<ClassifierParams>(
                rawConfig, ConfigKeys::CLASSIFICATION_CONFIG_ID);
            std::unique_ptr<IFrameProcessor> regionSource;
            if (params.regionMode == ClassifierRegionMode::Blob)
            {
                // Рамки берём у одноцветных blob; составные объекты и
                // отфильтрованные шаблоны выключаем в копии параметров.
                BlobParams blob = Automapper::mapParams<BlobParams>(
                    rawConfig, ConfigKeys::BLOB_DETECTION_CONFIG_ID);
                blob.enableOneColorDetection = true;
                blob.enableMultiColorDetection = false;
                blob.multiColorBlobParams.clear();
                if (!params.blobPatternIds.empty())
                    for (OneColorBlobParams& pattern : blob.oneColorBlobParams)
                        pattern.enabled = pattern.enabled &&
                            std::find(params.blobPatternIds.begin(), params.blobPatternIds.end(),
                                      pattern.id) != params.blobPatternIds.end();
                regionSource = std::make_unique<BlobProcessor>(blob);
            }
            return std::make_unique<ClassifierProcessor>(params, std::move(regionSource));
        }
#ifdef MTV3_BOARD
        case ProcessingType::ObjectDetection:
        {
            YoloParams params = rawConfig.at("object_detection").get<YoloParams>();
            return std::make_unique<RknnYoloProcessor>(params);
        }
#else
        case ProcessingType::ObjectDetection:
        {
            YoloParams params = rawConfig.at("object_detection").get<YoloParams>();
            return std::make_unique<YoloProcessor>(params);
        }
#endif
        /*
        case ProcessingType::Calibration:
            return createCalibrationProcessor(rawConfig);
        */
        default:
            throw std::runtime_error("Unknown processing type"); // exception, maybe use a custom exception class
    }
}
