#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "platform/serial.hpp"
#include "transport/vision_frame.hpp"

class BinaryUartTransport
{
public:
    BinaryUartTransport(std::string device, int baud, size_t maxObjects);
    ~BinaryUartTransport();

    bool isOpen() const { return port_.isOpen(); }
    void publish(const VisionFrame& frame);

private:
    bool openPort();
    bool fail(const std::string& reason);
    void run();
    void monitorRx();
    void sendFrame(const VisionFrame& frame);

    std::string device_;
    int baud_;
    size_t maxObjects_;
    platform::SerialPort port_;
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::condition_variable ready_;
    VisionFrame pendingFrame_;
    bool hasFrame_ = false;
    std::thread worker_;
    std::thread rxMonitor_;
};
