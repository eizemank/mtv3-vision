#pragma once
// ClassifierProcessor — NN-классификация области кадра.
// Backend: MTV3_BOARD -> RKNN (NPU, .rknn), десктоп -> cv::dnn (.onnx, >=3.4.1).
// Область задаёт params.regionMode (см. classifier_params.hpp):
//   whole — весь кадр; roi — прямоугольник из конфига;
//   blob  — рамки blob от regionSource (BlobProcessor), до maxRegions штук.
// Метаданные: по одному BlobMetaData на классифицированную область,
// id = индекс класса, confidence = area = score (0..1), boundingBox = область.

#include <memory>
#include <vector>

#include "model/classifier_params.hpp"
#include "processing/i_frame_processor.hpp"

#ifdef MTV3_BOARD
#include "nn/rknn_classifier.hpp"
#else
#include <opencv2/dnn.hpp>
#endif

class ClassifierProcessor : public IFrameProcessor
{
public:
    explicit ClassifierProcessor(const ClassifierParams& params,
                                 std::unique_ptr<IFrameProcessor> regionSource = nullptr);

    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;

    /// Области кадра для текущего режима (без инференса). В blob-режиме
    /// запускает regionSource, поэтому не const.
    std::vector<cv::Rect> regions(cv::Mat& frame);

private:
    std::vector<float> infer(const cv::Mat& bgr);   // скоры по классам

    ClassifierParams params_;
    std::unique_ptr<IFrameProcessor> regionSource_;
#ifdef MTV3_BOARD
    RknnClassifier net_;
#else
    cv::dnn::Net net_;
#endif
};
