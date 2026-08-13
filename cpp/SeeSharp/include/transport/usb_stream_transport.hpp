#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/core.hpp>

#include "transport/vision_frame.hpp"

class UsbStreamTransport
{
public:
    UsbStreamTransport(std::string device, int jpegQuality, int maxFps,
                       bool metadataEnabled, bool videoEnabled);
    ~UsbStreamTransport();

    void publish(const VisionFrame& frame, const cv::Mat& image);

private:
    struct PendingFrame
    {
        VisionFrame metadata;
        cv::Mat image;
    };

    void run();
    bool openDevice();
    void closeDevice();
    bool writeRecord(uint8_t messageType, uint32_t frameId,
                     const uint8_t* data, size_t size);
    std::string serializeMetadata(const VisionFrame& frame) const;

    std::string device_;
    int jpegQuality_ = 80;
    int maxFps_ = 15;
    bool metadataEnabled_ = true;
    bool videoEnabled_ = true;
    int deviceFd_ = -1;
    bool running_ = true;
    bool hasPendingFrame_ = false;
    PendingFrame pendingFrame_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
};
