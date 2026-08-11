#pragma once
// CameraSource — IFrameSource поверх cv::VideoCapture (аналог camera_source.py).
// Только десктоп: на модуле videoio не собирается, источник — ShmSource.

#include <opencv2/opencv.hpp>

#ifdef RASPBERRY_CM5
#include <chrono>
#include <iostream>
#include <thread>
#endif

#include "pipeline/i_frame_source.hpp"

class CameraSource : public IFrameSource
{
public:
    explicit CameraSource(int device = 0, int backend = cv::CAP_V4L2)
        : cap_(device, backend) {}

    bool read(cv::Mat& frame) override
    {
#ifdef RASPBERRY_CM5
        for (int attempt = 0; attempt < 50; ++attempt)
        {
            if (cap_.read(frame))
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cerr << "Camera returned no frames for 5 seconds" << std::endl;
        return false;
#else
        return cap_.read(frame);
#endif
    }
    bool isOpened() const override { return cap_.isOpened(); }
    void release() override { cap_.release(); }

    double fps() const
    {
        double f = cap_.get(cv::CAP_PROP_FPS);
        return f > 0 ? f : 25.0;
    }

private:
    cv::VideoCapture cap_;
};
