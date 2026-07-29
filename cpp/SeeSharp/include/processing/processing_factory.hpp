#pragma once
#include "config/config_reader.hpp"
#include "i_frame_processor.hpp"
#include <memory>

class ProcessingFactory
{
public:
    static std::unique_ptr<IFrameProcessor> createProcessor(const nlohmann::json& config);
};
