#pragma once
// RknnClassifier — тонкая обёртка librknn_api для классификатора на RV1126.
// Компилируется только с MTV3_BOARD (на десктопе инференс через cv::dnn).
// Модель: один вход uint8 NHWC RGB (квантованная, нормализация зашита при
// конвертации onnx2rknn.py), один выход float (логиты/скоры классов).

#include <cstdint>
#include <string>
#include <vector>

class RknnClassifier
{
public:
    RknnClassifier() = default;
    ~RknnClassifier();

    RknnClassifier(const RknnClassifier&) = delete;
    RknnClassifier& operator=(const RknnClassifier&) = delete;

    bool init(const std::string& modelPath);
    bool valid() const { return ctx_ != 0; }

    /// rgb — плотный буфер HxWx3 (uint8, RGB); выход — float-скоры по классам
    std::vector<float> infer(const uint8_t* rgb, int bytes);

private:
    uint64_t ctx_ = 0;        // rknn_context (typedef в rknn_api.h)
    uint32_t nOutputs_ = 0;
};
