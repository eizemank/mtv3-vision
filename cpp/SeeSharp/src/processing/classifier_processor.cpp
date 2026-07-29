#include "processing/classifier_processor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

ClassifierProcessor::ClassifierProcessor(const ClassifierParams& params)
    : params_(params)
{
#ifdef MTV3_BOARD
    if (!net_.init(params_.modelRknn))
        throw std::runtime_error("ClassifierProcessor: rknn init failed: " +
                                 params_.modelRknn);
#else
    net_ = cv::dnn::readNetFromONNX(params_.modelOnnx);
    if (net_.empty())
        throw std::runtime_error("ClassifierProcessor: can't load " +
                                 params_.modelOnnx);
#endif
}

std::vector<float> ClassifierProcessor::infer(const cv::Mat& bgr)
{
    const int s = params_.inputSize;
    cv::Mat resized;
    cv::resize(bgr, resized, cv::Size(s, s), 0, 0, cv::INTER_AREA);

#ifdef MTV3_BOARD
    // NPU: uint8 RGB NHWC, нормализация (x/255) зашита при конвертации
    cv::Mat rgb;
    cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
    if (!rgb.isContinuous())
        rgb = rgb.clone();
    return net_.infer(rgb.data, s * s * 3);
#else
    // cv::dnn: float NCHW, RGB (swapRB), x/255 — как ToTensor при обучении
    cv::Mat blob = cv::dnn::blobFromImage(resized, 1.0 / 255.0,
                                          cv::Size(s, s), cv::Scalar(),
                                          /*swapRB=*/true, /*crop=*/false);
    net_.setInput(blob);
    cv::Mat out = net_.forward();
    return std::vector<float>(out.ptr<float>(),
                              out.ptr<float>() + out.total());
#endif
}

std::pair<cv::Mat, std::vector<BlobMetaData>> ClassifierProcessor::process(cv::Mat& frame)
{
    cv::Mat result = frame.clone();
    std::vector<BlobMetaData> metadata;

    std::vector<float> logits = infer(frame);
    if (logits.empty())
        return { result, metadata };

    // softmax (модель отдаёт логиты)
    float mx = *std::max_element(logits.begin(), logits.end());
    double sum = 0.0;
    for (float& v : logits)
    {
        v = std::exp(v - mx);
        sum += v;
    }
    int best = (int)(std::max_element(logits.begin(), logits.end()) -
                     logits.begin());
    float score = (float)(logits[best] / sum);

    std::string label = best < (int)params_.classNames.size()
                            ? params_.classNames[best]
                            : "class" + std::to_string(best);
    char text[128];
    snprintf(text, sizeof(text), "%s %.2f", label.c_str(), score);
    bool accepted = score >= params_.scoreThreshold;
    cv::putText(result, text, { 10, 30 }, cv::FONT_HERSHEY_SIMPLEX, 0.9,
                accepted ? cv::Scalar(0, 220, 0) : cv::Scalar(128, 128, 128), 2);

    if (accepted)
    {
        BlobMetaData meta;
        meta.id = best;
        meta.center = { frame.cols / 2.0f, frame.rows / 2.0f };
        meta.area = score;               // score в поле area (0..1)
        meta.boundingBox = { 0, 0, frame.cols, frame.rows };
        metadata.push_back(meta);
    }
    return { result, metadata };
}
