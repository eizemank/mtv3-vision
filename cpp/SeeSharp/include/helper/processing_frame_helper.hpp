#pragma once

#include <opencv2/core.hpp>

namespace ProcessingFrameHelper
{
    inline cv::Mat getGrayFrame(cv::Mat& frame)
    {
        cv::Mat gray;
        if (frame.channels() == 3)
        {
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        }
        else
        {
            gray = frame.clone();
        }
        return gray;
    }
}