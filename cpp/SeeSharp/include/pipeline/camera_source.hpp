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
    explicit CameraSource(int device = 0, int backend = cv::CAP_V4L2,
                          int width = 0, int height = 0, int requestedFps = 0,
                          bool useMjpeg = false)
        : device_(device), backend_(backend), cap_(device, backend)
    {
        if (!cap_.isOpened())
            return;
        if (useMjpeg)
            cap_.set(cv::CAP_PROP_FOURCC,
                     cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
        if (width > 0)
            cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
        if (height > 0)
            cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
        if (requestedFps > 0)
            cap_.set(cv::CAP_PROP_FPS, requestedFps);
        cap_.set(cv::CAP_PROP_BUFFERSIZE, 1);
    }

    bool read(cv::Mat& frame) override
    {
#ifdef RASPBERRY_CM5
        while (true)
        {
            if (cap_.read(frame))
                return true;
            std::cerr << "Camera stream stalled; reopening /dev/video"
                      << device_ << std::endl;
            cap_.release();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!cap_.open(device_, backend_))
                std::cerr << "Failed to reopen /dev/video" << device_ << std::endl;
        }
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
    int device_;
    int backend_;
    cv::VideoCapture cap_;
};
