#pragma once
// ClassifierProcessor — NN-классификация всего кадра.
// Backend: MTV3_BOARD -> RKNN (NPU, .rknn), десктоп -> cv::dnn (.onnx, >=3.4.1).
// Метаданные: один BlobMetaData, id = индекс класса, area = score (0..1),
// boundingBox = весь кадр.

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
    explicit ClassifierProcessor(const ClassifierParams& params);

    std::pair<cv::Mat, std::vector<BlobMetaData>> process(cv::Mat& frame) override;

private:
    std::vector<float> infer(const cv::Mat& bgr);   // скоры по классам

    ClassifierParams params_;
#ifdef MTV3_BOARD
    RknnClassifier net_;
#else
    cv::dnn::Net net_;
#endif
};
