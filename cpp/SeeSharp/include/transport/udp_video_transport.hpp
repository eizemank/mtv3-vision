#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/core.hpp>

class UdpVideoTransport
{
public:
    UdpVideoTransport(std::string host, uint16_t port, int jpegQuality,
                      size_t packetSize, int maxFps);
    ~UdpVideoTransport();

    bool isOpen() const { return socketFd_ >= 0; }
    void publish(uint32_t frameId, uint32_t timestampMs, const cv::Mat& image);

private:
    struct PendingFrame
    {
        uint32_t frameId = 0;
        uint32_t timestampMs = 0;
        cv::Mat image;
    };

    void run();
    void sendFrame(const PendingFrame& frame);

    int socketFd_ = -1;
    uint32_t address_ = 0;
    uint16_t port_ = 0;
    int jpegQuality_ = 80;
    size_t packetSize_ = 1400;
    int maxFps_ = 15;
    bool running_ = false;
    bool hasPendingFrame_ = false;
    PendingFrame pendingFrame_;
    std::chrono::steady_clock::time_point lastAccepted_{};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
};
