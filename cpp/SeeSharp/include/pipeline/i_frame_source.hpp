#pragma once
// Абстракция источника кадров (аналог FrameSource ABC в SeeSharpPy).
// DIP: Pipeline зависит от интерфейса, а не от cv::VideoCapture/shm.

#include <opencv2/core.hpp>

class IFrameSource
{
public:
    /// false — источник иссяк/умер (конец видео, таймаут демона)
    virtual bool read(cv::Mat& frame) = 0;
    virtual bool isOpened() const = 0;
    virtual void release() = 0;
    virtual ~IFrameSource() = default;
};
