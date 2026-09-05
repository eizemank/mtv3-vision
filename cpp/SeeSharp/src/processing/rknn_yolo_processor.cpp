#include "processing/rknn_yolo_processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/imgproc.hpp>
#include <rknn_api.h>

namespace
{
float intersectionOverUnion(const cv::Rect& first, const cv::Rect& second)
{
    const cv::Rect intersection = first & second;
    const float intersectionArea = static_cast<float>(intersection.area());
    const float unionArea = static_cast<float>(first.area() + second.area()) -
                            intersectionArea;
    return unionArea > 0.0f ? intersectionArea / unionArea : 0.0f;
}

std::vector<int> nonMaximumSuppression(const std::vector<cv::Rect>& boxes,
                                       const std::vector<float>& scores,
                                       const std::vector<int>& classIds,
                                       float threshold, int limit)
{
    std::vector<int> order(boxes.size());
    for (size_t index = 0; index < order.size(); ++index)
        order[index] = static_cast<int>(index);
    std::sort(order.begin(), order.end(), [&](int first, int second) {
        return scores[first] > scores[second];
    });

    std::vector<int> kept;
    for (int candidate : order)
    {
        bool suppressed = false;
        for (int selected : kept)
        {
            if (classIds[candidate] == classIds[selected] &&
                intersectionOverUnion(boxes[candidate], boxes[selected]) > threshold)
            {
                suppressed = true;
                break;
            }
        }
        if (!suppressed)
        {
            kept.push_back(candidate);
            if (static_cast<int>(kept.size()) >= limit)
                break;
        }
    }
    return kept;
}
}

RknnYoloProcessor::RknnYoloProcessor(const YoloParams& params) : params_(params)
{
    if (!params_.classNamesFile.empty())
    {
        std::ifstream labels(params_.classNamesFile);
        if (!labels)
            throw std::runtime_error("RknnYoloProcessor: can't load " +
                                     params_.classNamesFile);
        std::string label;
        while (std::getline(labels, label))
            if (!label.empty())
                params_.classNames.push_back(label);
    }
    params_.maxObjects = std::max(1, std::min(params_.maxObjects, 100));
    const int minimumAttributes = params_.outputHasObjectness ? 6 : 5;
    if (params_.outputAttributes < minimumAttributes)
        throw std::runtime_error("RknnYoloProcessor: too few output attributes");
    if (params_.outputLayout != "channels_first" &&
        params_.outputLayout != "predictions_first")
        throw std::runtime_error("RknnYoloProcessor: unsupported output_layout");
    if (!loadModel())
        throw std::runtime_error("RknnYoloProcessor: can't load " + params_.modelRknn);
}

RknnYoloProcessor::~RknnYoloProcessor()
{
    if (context_)
        rknn_destroy(static_cast<rknn_context>(context_));
}

bool RknnYoloProcessor::loadModel()
{
    FILE* file = fopen(params_.modelRknn.c_str(), "rb");
    if (!file)
        return false;
    fseek(file, 0, SEEK_END);
    const long modelSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (modelSize <= 0)
    {
        fclose(file);
        return false;
    }
    std::vector<uint8_t> model(static_cast<size_t>(modelSize));
    const bool readOk = fread(model.data(), 1, model.size(), file) == model.size();
    fclose(file);
    if (!readOk)
        return false;

    rknn_context context = 0;
    if (rknn_init(&context, model.data(), model.size(), 0) != RKNN_SUCC)
        return false;
    context_ = context;

    rknn_input_output_num io{};
    if (rknn_query(context, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io)) != RKNN_SUCC ||
        io.n_input != 1 || io.n_output != 1)
    {
        rknn_destroy(context);
        context_ = 0;
        return false;
    }
    rknn_tensor_attr output{};
    output.index = 0;
    if (rknn_query(context, RKNN_QUERY_OUTPUT_ATTR, &output, sizeof(output)) != RKNN_SUCC)
    {
        rknn_destroy(context);
        context_ = 0;
        return false;
    }
    outputElements_ = output.n_elems;
    if (outputElements_ < static_cast<uint32_t>(params_.outputAttributes))
    {
        rknn_destroy(context);
        context_ = 0;
        return false;
    }
    return true;
}

std::vector<float> RknnYoloProcessor::infer(const cv::Mat& rgb)
{
    rknn_input input{};
    input.index = 0;
    input.type = RKNN_TENSOR_UINT8;
    input.fmt = RKNN_TENSOR_NHWC;
    input.size = rgb.total() * rgb.elemSize();
    input.buf = const_cast<uint8_t*>(rgb.ptr<uint8_t>());
    if (rknn_inputs_set(static_cast<rknn_context>(context_), 1, &input) != RKNN_SUCC ||
        rknn_run(static_cast<rknn_context>(context_), nullptr) != RKNN_SUCC)
        return {};

    rknn_output output{};
    output.index = 0;
    output.want_float = 1;
    if (rknn_outputs_get(static_cast<rknn_context>(context_), 1, &output, nullptr) !=
        RKNN_SUCC)
        return {};
    const float* values = static_cast<const float*>(output.buf);
    const size_t count = output.size / sizeof(float);
    std::vector<float> result(values, values + count);
    rknn_outputs_release(static_cast<rknn_context>(context_), 1, &output);
    return result;
}

std::pair<cv::Mat, std::vector<BlobMetaData>> RknnYoloProcessor::process(cv::Mat& frame)
{
    cv::Mat result = frame.clone();
    const float scale = std::min(static_cast<float>(params_.inputWidth) / frame.cols,
                                 static_cast<float>(params_.inputHeight) / frame.rows);
    const int resizedWidth = std::max(1, cvRound(frame.cols * scale));
    const int resizedHeight = std::max(1, cvRound(frame.rows * scale));
    const int padX = (params_.inputWidth - resizedWidth) / 2;
    const int padY = (params_.inputHeight - resizedHeight) / 2;
    cv::Mat resized, letterbox(params_.inputHeight, params_.inputWidth,
                               CV_8UC3, cv::Scalar(114, 114, 114));
    cv::resize(frame, resized, {resizedWidth, resizedHeight});
    resized.copyTo(letterbox(cv::Rect(padX, padY, resizedWidth, resizedHeight)));
    cv::cvtColor(letterbox, letterbox, cv::COLOR_BGR2RGB);

    const std::vector<float> output = infer(letterbox);
    const int attributes = params_.outputAttributes;
    if (output.empty() || output.size() % static_cast<size_t>(attributes) != 0)
        throw std::runtime_error("RknnYoloProcessor: unexpected output tensor size");
    const int predictions = static_cast<int>(output.size() / attributes);
    auto value = [&](int prediction, int attribute) -> float {
        if (params_.outputLayout == "channels_first")
            return output[static_cast<size_t>(attribute) * predictions + prediction];
        return output[static_cast<size_t>(prediction) * attributes + attribute];
    };

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;
    for (int prediction = 0; prediction < predictions; ++prediction)
    {
        const int classOffset = params_.outputHasObjectness ? 5 : 4;
        const float objectness = params_.outputHasObjectness
                                     ? value(prediction, 4) : 1.0f;
        int classId = 0;
        float score = value(prediction, classOffset) * objectness;
        for (int attribute = classOffset + 1; attribute < attributes; ++attribute)
        {
            const float candidate = value(prediction, attribute) * objectness;
            if (candidate > score)
            {
                score = candidate;
                classId = attribute - classOffset;
            }
        }
        if (score < params_.confidenceThreshold)
            continue;
        const float centerX = (value(prediction, 0) - padX) / scale;
        const float centerY = (value(prediction, 1) - padY) / scale;
        const float width = value(prediction, 2) / scale;
        const float height = value(prediction, 3) / scale;
        cv::Rect box(cvRound(centerX - width * 0.5f),
                     cvRound(centerY - height * 0.5f),
                     cvRound(width), cvRound(height));
        box &= cv::Rect(0, 0, frame.cols, frame.rows);
        if (!box.empty())
        {
            boxes.push_back(box);
            scores.push_back(score);
            classIds.push_back(classId);
        }
    }

    const std::vector<int> kept = nonMaximumSuppression(
        boxes, scores, classIds, params_.nmsThreshold, params_.maxObjects);
    std::vector<BlobMetaData> metadata;
    for (int index : kept)
    {
        const cv::Rect& box = boxes[index];
        const int classId = classIds[index];
        const std::string label = classId < static_cast<int>(params_.classNames.size())
                                      ? params_.classNames[classId]
                                      : "class" + std::to_string(classId);
        cv::rectangle(result, box, cv::Scalar(0, 220, 0), 2);
        cv::putText(result, label + " " + cv::format("%.2f", scores[index]),
                    {box.x, std::max(18, box.y)}, cv::FONT_HERSHEY_SIMPLEX,
                    0.55, cv::Scalar(0, 220, 0), 2);
        BlobMetaData item;
        item.id = classId;
        item.center = {box.x + box.width * 0.5f, box.y + box.height * 0.5f};
        item.area = scores[index];
        item.confidence = scores[index];
        item.boundingBox = box;
        metadata.push_back(item);
    }
    return {result, metadata};
}
