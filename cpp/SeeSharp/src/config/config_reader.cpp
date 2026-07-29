#include "config/config_reader.hpp"

bool ConfigReader::loadFromFile(const std::string& filepath)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        return false;
    }
    file >> config_;
    return true;
}

nlohmann::json ConfigReader::getRawConfig() const
{
    return config_;
}
