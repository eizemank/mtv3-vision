#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct YoloParams
{
    std::string modelOnnx = "yolo11n.onnx";
    std::string classNamesFile = "coco.names";
    std::vector<std::string> classNames;
    int inputWidth = 640;
    int inputHeight = 640;
    float confidenceThreshold = 0.35f;
    float nmsThreshold = 0.45f;
    int maxObjects = 10;
};

inline void from_json(const nlohmann::json& json, YoloParams& params)
{
    params.modelOnnx = json.value("model_onnx", "yolo11n.onnx");
    params.classNamesFile = json.value("class_names_file", "coco.names");
    params.classNames = json.value("class_names", std::vector<std::string>{});
    params.inputWidth = json.value("input_width", 640);
    params.inputHeight = json.value("input_height", 640);
    params.confidenceThreshold = json.value("confidence_threshold", 0.35f);
    params.nmsThreshold = json.value("nms_threshold", 0.45f);
    params.maxObjects = json.value("max_objects", 10);
}
