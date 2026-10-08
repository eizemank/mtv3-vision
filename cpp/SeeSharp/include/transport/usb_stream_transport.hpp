#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/core.hpp>

#include "platform/serial.hpp"
#include "transport/vision_frame.hpp"

class UsbStreamTransport
{
public:
    UsbStreamTransport(std::string device, int jpegQuality, int maxFps, int maxWidth,
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
    int maxWidth_ = 960;
    bool metadataEnabled_ = true;
    bool videoEnabled_ = true;
    platform::SerialPort port_;
    bool running_ = true;
    bool hasPendingFrame_ = false;
    PendingFrame pendingFrame_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::chrono::steady_clock::time_point nextFrame_{};
};
