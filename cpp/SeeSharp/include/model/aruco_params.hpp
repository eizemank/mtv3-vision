#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct ArucoParams
{
    std::string dictionary = "DICT_4X4_50";
    double markerLength = 0.05;
    double minArea = 0.0;
    double maxArea = 1.0e12;
    std::vector<int> allowedIds;
};

inline void from_json(const nlohmann::json& json, ArucoParams& params)
{
    params.dictionary = json.value("dictionary", "DICT_4X4_50");
    params.markerLength = json.value("marker_length", 0.05);
    params.minArea = json.value("min_area", 0.0);
    params.maxArea = json.value("max_area", 1.0e12);
    params.allowedIds = json.value("allowed_ids", std::vector<int>{});
}
