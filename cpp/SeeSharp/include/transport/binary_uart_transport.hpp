#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "transport/vision_frame.hpp"

class BinaryUartTransport
{
public:
    BinaryUartTransport(std::string device, int baud, size_t maxObjects);
    ~BinaryUartTransport();

    bool isOpen() const { return fd_ >= 0; }
    void publish(const VisionFrame& frame);

private:
    bool openPort();
    void run();
    void sendFrame(const VisionFrame& frame);

    std::string device_;
    int baud_;
    size_t maxObjects_;
    int fd_ = -1;
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::condition_variable ready_;
    VisionFrame pendingFrame_;
    bool hasFrame_ = false;
    std::thread worker_;
};
