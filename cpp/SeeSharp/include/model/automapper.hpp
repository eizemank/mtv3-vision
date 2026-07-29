#pragma once

#include "nlohmann/json.hpp"
#include "helper/json_helper.hpp"

class Automapper
{
public:
    template<typename T>
    static T mapParams(const nlohmann::json& rawConfig, const std::string& configId)
    {
        const auto& config = rawConfig.at(configId);
        return config.get<T>();
    }
};
