#include "transport/udp_metadata_transport.hpp"

#include <utility>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include "transport/metadata_json.hpp"

UdpMetadataTransport::UdpMetadataTransport(std::string host, uint16_t port)
    : port_(port)
{
    socketFd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (socketFd_ < 0 || inet_pton(AF_INET, host.c_str(), &address_) != 1)
    {
        if (socketFd_ >= 0)
            close(socketFd_);
        socketFd_ = -1;
    }
    else
    {
        running_ = true;
        worker_ = std::thread(&UdpMetadataTransport::run, this);
    }
}

UdpMetadataTransport::~UdpMetadataTransport()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (socketFd_ >= 0)
        close(socketFd_);
}

void UdpMetadataTransport::publish(const VisionFrame& frame)
{
    if (socketFd_ < 0)
        return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pendingFrame_ = frame;
        hasPendingFrame_ = true;
    }
    condition_.notify_one();
}

void UdpMetadataTransport::run()
{
    while (true)
    {
        VisionFrame frame;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [&] { return hasPendingFrame_ || !running_; });
            if (!running_ && !hasPendingFrame_)
                break;
            frame = std::move(pendingFrame_);
            hasPendingFrame_ = false;
        }
        sendFrame(frame);
    }
}

void UdpMetadataTransport::sendFrame(const VisionFrame& frame)
{
    const std::string payload = serializeVisionFrameJson(frame).dump();

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port_);
    destination.sin_addr.s_addr = address_;
    sendto(socketFd_, payload.data(), payload.size(), MSG_DONTWAIT,
           reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
}
