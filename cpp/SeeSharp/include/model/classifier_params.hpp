#pragma once
// Параметры NN-классификатора (секция "classification" в config.json).
// Препроцесс фиксирован и обязан совпадать во всех трёх местах:
// обучение (simple_classifier.py), конвертация (onnx2rknn.py), инференс здесь:
//   RGB, resize до input_size x input_size, нормализация x/255.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

struct ClassifierParams
{
    std::string modelOnnx;               // десктоп: cv::dnn
    std::string modelRknn;               // модуль: librknn_api
    int inputSize = 64;
    int resizeSize = 64;
    bool centerCrop = false;
    bool swapRb = true;
    std::vector<float> mean{0.0f, 0.0f, 0.0f};
    std::vector<float> standardDeviation{1.0f, 1.0f, 1.0f};
    float scoreThreshold = 0.5f;
    std::vector<std::string> classNames;
    std::string classNamesFile;
};

inline void from_json(const nlohmann::json& j, ClassifierParams& p)
{
    p.modelOnnx = j.value("model_onnx", "simple_classifier.onnx");
    p.modelRknn = j.value("model_rknn", "simple_classifier.rknn");
    p.inputSize = j.value("input_size", 64);
    p.resizeSize = j.value("resize_size", p.inputSize);
    p.centerCrop = j.value("center_crop", false);
    p.swapRb = j.value("swap_rb", true);
    if (j.contains("mean"))
        j.at("mean").get_to(p.mean);
    if (j.contains("std"))
        j.at("std").get_to(p.standardDeviation);
    p.scoreThreshold = j.value("score_threshold", 0.5f);
    if (j.contains("class_names"))
        j.at("class_names").get_to(p.classNames);
    p.classNamesFile = j.value("class_names_file", "");
}
