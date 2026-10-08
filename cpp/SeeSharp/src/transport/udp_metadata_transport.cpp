#include "transport/udp_metadata_transport.hpp"

#include <utility>

#include "transport/metadata_json.hpp"

using platform::socketValid;

UdpMetadataTransport::UdpMetadataTransport(std::string host, uint16_t port)
    : port_(port)
{
    platform::socketsInit();
    socketFd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (!socketValid(socketFd_) || !platform::socketSetNonBlocking(socketFd_) ||
        inet_pton(AF_INET, host.c_str(), &address_) != 1)
    {
        platform::socketClose(socketFd_);
        socketFd_ = platform::kInvalidSocket;
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
    platform::socketClose(socketFd_);
}

void UdpMetadataTransport::publish(const VisionFrame& frame)
{
    if (!socketValid(socketFd_))
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
    platform::socketSendTo(socketFd_, payload.data(), payload.size(), destination);
}
