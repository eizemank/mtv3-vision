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
    float scoreThreshold = 0.5f;
    std::vector<std::string> classNames;
};

inline void from_json(const nlohmann::json& j, ClassifierParams& p)
{
    p.modelOnnx = j.value("model_onnx", "simple_classifier.onnx");
    p.modelRknn = j.value("model_rknn", "simple_classifier.rknn");
    p.inputSize = j.value("input_size", 64);
    p.scoreThreshold = j.value("score_threshold", 0.5f);
    if (j.contains("class_names"))
        j.at("class_names").get_to(p.classNames);
}
