#include "processing/yolo_processor.hpp"

#include <algorithm>
#include <fstream>
#include <stdexcept>

#include <opencv2/imgproc.hpp>

YoloProcessor::YoloProcessor(const YoloParams& params) : params_(params)
{
    if (!params_.classNamesFile.empty())
    {
        std::ifstream labels(params_.classNamesFile);
        if (!labels)
            throw std::runtime_error("YoloProcessor: can't load " +
                                     params_.classNamesFile);
        std::string label;
        while (std::getline(labels, label))
            if (!label.empty())
                params_.classNames.push_back(label);
    }
    params_.maxObjects = std::clamp(params_.maxObjects, 1, 100);
    net_ = cv::dnn::readNetFromONNX(params_.modelOnnx);
    if (net_.empty())
        throw std::runtime_error("YoloProcessor: can't load " + params_.modelOnnx);
}

std::pair<cv::Mat, std::vector<BlobMetaData>> YoloProcessor::process(cv::Mat& frame)
{
    cv::Mat result = frame.clone();
    const float scale = std::min(
        static_cast<float>(params_.inputWidth) / frame.cols,
        static_cast<float>(params_.inputHeight) / frame.rows);
    const int resizedWidth = std::max(1, cvRound(frame.cols * scale));
    const int resizedHeight = std::max(1, cvRound(frame.rows * scale));
    const int padX = (params_.inputWidth - resizedWidth) / 2;
    const int padY = (params_.inputHeight - resizedHeight) / 2;
    cv::Mat resized, letterbox(params_.inputHeight, params_.inputWidth,
                               CV_8UC3, cv::Scalar(114, 114, 114));
    cv::resize(frame, resized, {resizedWidth, resizedHeight});
    resized.copyTo(letterbox(cv::Rect(padX, padY, resizedWidth, resizedHeight)));

    net_.setInput(cv::dnn::blobFromImage(letterbox, 1.0 / 255.0,
                                         {params_.inputWidth, params_.inputHeight},
                                         cv::Scalar(), true, false));
    cv::Mat output = net_.forward();
    if (output.dims != 3)
        throw std::runtime_error("YoloProcessor: expected a 3D detection tensor");

    const int first = output.size[1];
    const int second = output.size[2];
    const bool channelsFirst = first < second;
    const int attributes = channelsFirst ? first : second;
    const int predictions = channelsFirst ? second : first;
    if (attributes < 5)
        throw std::runtime_error("YoloProcessor: invalid output attributes");
    if (channelsFirst)
    {
        cv::Mat predictionsMatrixChannels(attributes, predictions, CV_32F,
                                           output.ptr<float>());
        cv::Mat predictionsMatrix;
        cv::transpose(predictionsMatrixChannels, predictionsMatrix);
        output = predictionsMatrix;
    }
    else
    {
        output = cv::Mat(predictions, attributes, CV_32F, output.ptr<float>()).clone();
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;
    for (int row = 0; row < output.rows; ++row)
    {
        const float* values = output.ptr<float>(row);
        cv::Mat classScores(1, attributes - 4, CV_32F,
                            const_cast<float*>(values + 4));
        cv::Point classIdPoint;
        double score;
        cv::minMaxLoc(classScores, nullptr, &score, nullptr, &classIdPoint);
        if (score < params_.confidenceThreshold)
            continue;
        const float cx = (values[0] - padX) / scale;
        const float cy = (values[1] - padY) / scale;
        const float width = values[2] / scale;
        const float height = values[3] / scale;
        cv::Rect box(cvRound(cx - width * 0.5f), cvRound(cy - height * 0.5f),
                     cvRound(width), cvRound(height));
        box &= cv::Rect(0, 0, frame.cols, frame.rows);
        if (box.empty())
            continue;
        boxes.push_back(box);
        scores.push_back(static_cast<float>(score));
        classIds.push_back(classIdPoint.x);
    }

    std::vector<int> kept;
    cv::dnn::NMSBoxes(boxes, scores, params_.confidenceThreshold,
                      params_.nmsThreshold, kept, 1.0f, params_.maxObjects);
    std::vector<BlobMetaData> metadata;
    for (int index : kept)
    {
        const cv::Rect& box = boxes[index];
        const int classId = classIds[index];
        const std::string label = classId < static_cast<int>(params_.classNames.size())
                                      ? params_.classNames[classId]
                                      : "class" + std::to_string(classId);
        const std::string text = label + " " +
                                 cv::format("%.2f", scores[index]);
        cv::rectangle(result, box, cv::Scalar(0, 220, 0), 2);
        cv::putText(result, text, {box.x, std::max(18, box.y)},
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 220, 0), 2);
        BlobMetaData metadataItem;
        metadataItem.id = classId;
        metadataItem.center = {box.x + box.width * 0.5f,
                               box.y + box.height * 0.5f};
        metadataItem.area = scores[index];
        metadataItem.confidence = scores[index];
        metadataItem.boundingBox = box;
        metadata.push_back(metadataItem);
    }
    return {result, metadata};
}
