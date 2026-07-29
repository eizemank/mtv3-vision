#pragma once

#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

class ConfigReader
{
public:
    bool loadFromFile(const std::string& filepath);
    nlohmann::json getRawConfig() const;

private:
    nlohmann::json config_;
};