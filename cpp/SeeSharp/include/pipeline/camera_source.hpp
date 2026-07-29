#pragma once
// CameraSource — IFrameSource поверх cv::VideoCapture (аналог camera_source.py).
// Только десктоп: на модуле videoio не собирается, источник — ShmSource.

#include <opencv2/opencv.hpp>

#include "pipeline/i_frame_source.hpp"

class CameraSource : public IFrameSource
{
public:
    explicit CameraSource(int device = 0, int backend = cv::CAP_V4L2)
        : cap_(device, backend) {}

    bool read(cv::Mat& frame) override { return cap_.read(frame); }
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
