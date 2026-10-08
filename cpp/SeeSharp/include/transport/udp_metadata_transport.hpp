#pragma once

#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "platform/socket.hpp"
#include "transport/vision_frame.hpp"

class UdpMetadataTransport
{
public:
    UdpMetadataTransport(std::string host, uint16_t port);
    ~UdpMetadataTransport();

    bool isOpen() const { return platform::socketValid(socketFd_); }
    void publish(const VisionFrame& frame);

private:
    void run();
    void sendFrame(const VisionFrame& frame);

    platform::socket_t socketFd_ = platform::kInvalidSocket;
    uint32_t address_ = 0;
    uint16_t port_ = 0;
    bool running_ = false;
    bool hasPendingFrame_ = false;
    VisionFrame pendingFrame_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
};
