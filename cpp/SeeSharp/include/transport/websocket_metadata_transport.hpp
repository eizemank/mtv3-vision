#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "transport/vision_frame.hpp"

class WebSocketMetadataTransport
{
public:
    WebSocketMetadataTransport(std::string bindAddress, uint16_t port);
    ~WebSocketMetadataTransport();

    bool isOpen() const { return listenerFd_ >= 0; }
    void publish(const VisionFrame& frame);

private:
    void run();

    std::string bindAddress_;
    uint16_t port_ = 0;
    int listenerFd_ = -1;
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::condition_variable ready_;
    VisionFrame pendingFrame_;
    bool hasFrame_ = false;
    std::thread worker_;
};
