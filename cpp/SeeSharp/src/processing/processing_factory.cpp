#include "processing/processing_factory.hpp"
#include "model/common_config.hpp"
#include "model/blob_detection/blob_params.hpp"
#include "processing/blob_processor.hpp"
#include "processing/line_processor.hpp"
#include "processing/circle_processor.hpp"
#include "model/automapper.hpp"
#include "config/config_keys.hpp"
#include "model/general_params.hpp"
#include "processing/aruco_processor.hpp"
#include "model/classifier_params.hpp"
#include "processing/classifier_processor.hpp"
#include "processing/passthrough_processor.hpp"

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
            // TODO (DD): Do we need aruco params?
            return std::make_unique<ArucoProcessor>();
        }
        case ProcessingType::Classification:
        {
            ClassifierParams params = Automapper::mapParams<ClassifierParams>(rawConfig, ConfigKeys::CLASSIFICATION_CONFIG_ID);
            return std::make_unique<ClassifierProcessor>(params);
        }
        /*
        case ProcessingType::Calibration:
            return createCalibrationProcessor(rawConfig);
        */
        default:
            throw std::runtime_error("Unknown processing type"); // exception, maybe use a custom exception class
    }
}
