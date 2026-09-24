#pragma once
// Параметры NN-классификатора (секция "classification" в config.json).
// Препроцесс фиксирован и обязан совпадать во всех трёх местах:
// обучение (simple_classifier.py), конвертация (onnx2rknn.py), инференс здесь:
//   RGB, resize до input_size x input_size, нормализация x/255.
//
// region_mode задаёт, какую часть кадра классифицировать:
//   whole — весь кадр (как раньше);
//   roi   — прямоугольник roi = [x, y, w, h] в долях кадра (0..1);
//   blob  — рамки одноцветных blob из секции blob_detection
//           (blob_pattern_ids: [] = все включённые шаблоны).
// Вырезка области (region_crop.hpp) общая для инференса и сбора датасета.

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

enum class ClassifierRegionMode
{
    Whole,
    Roi,
    Blob,
};

inline ClassifierRegionMode classifierRegionModeFromString(const std::string& value)
{
    if (value == "whole") return ClassifierRegionMode::Whole;
    if (value == "roi") return ClassifierRegionMode::Roi;
    if (value == "blob") return ClassifierRegionMode::Blob;
    throw std::invalid_argument("classification.region_mode must be whole, roi or blob: " + value);
}

inline const char* classifierRegionModeToString(ClassifierRegionMode mode)
{
    switch (mode)
    {
        case ClassifierRegionMode::Roi: return "roi";
        case ClassifierRegionMode::Blob: return "blob";
        default: return "whole";
    }
}

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

    ClassifierRegionMode regionMode = ClassifierRegionMode::Whole;
    std::array<float, 4> roi{0.25f, 0.25f, 0.5f, 0.5f};   // x, y, w, h в долях кадра
    std::vector<int> blobPatternIds;                       // пусто = все включённые
    float cropPadding = 0.1f;                              // доля размера рамки с каждой стороны
    int maxRegions = 8;                                    // blob-режим: сколько рамок за кадр
};

inline void validateClassifierRoi(const std::array<float, 4>& roi)
{
    for (float value : roi)
        if (!(value >= 0.0f && value <= 1.0f))
            throw std::invalid_argument("classification.roi values must be within 0..1");
    if (roi[2] <= 0.0f || roi[3] <= 0.0f)
        throw std::invalid_argument("classification.roi width and height must be positive");
    if (roi[0] + roi[2] > 1.0001f || roi[1] + roi[3] > 1.0001f)
        throw std::invalid_argument("classification.roi must stay inside the frame");
}

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

    p.regionMode = classifierRegionModeFromString(j.value("region_mode", "whole"));
    if (j.contains("roi"))
    {
        const auto roi = j.at("roi").get<std::vector<float>>();
        if (roi.size() != 4)
            throw std::invalid_argument("classification.roi must contain 4 values: x, y, w, h");
        for (size_t i = 0; i < 4; ++i)
            p.roi[i] = roi[i];
    }
    validateClassifierRoi(p.roi);
    if (j.contains("blob_pattern_ids"))
        j.at("blob_pattern_ids").get_to(p.blobPatternIds);
    p.cropPadding = j.value("crop_padding", 0.1f);
    if (p.cropPadding < 0.0f || p.cropPadding > 1.0f)
        throw std::invalid_argument("classification.crop_padding must be within 0..1");
    p.maxRegions = j.value("max_regions", 8);
    if (p.maxRegions < 1 || p.maxRegions > 64)
        throw std::invalid_argument("classification.max_regions must be within 1..64");
    if (p.inputSize < 8 || p.inputSize > 1024)
        throw std::invalid_argument("classification.input_size must be within 8..1024");
}
