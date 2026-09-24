#pragma once
#include <opencv2/core.hpp>
#include "model/blob_detection/color_model.hpp"

// Input channels must already be in the selected color model.
inline cv::Mat blobColorMask(const cv::Mat& converted, blob_color::Model model,
                             const cv::Scalar& lower, const cv::Scalar& upper)
{
    cv::Mat mask;
    if (blob_color::hueModel(model) && lower[0] > upper[0]) {
        cv::Mat other;
        auto highEnd=upper; highEnd[0]=360;
        auto lowEnd=lower; lowEnd[0]=0;
        cv::inRange(converted,lower,highEnd,mask);
        cv::inRange(converted,lowEnd,upper,other);
        cv::bitwise_or(mask,other,mask);
    } else cv::inRange(converted,lower,upper,mask);
    return mask;
}
