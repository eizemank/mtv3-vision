#include "transport/udp_video_transport.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <opencv2/imgcodecs.hpp>

namespace
{
constexpr size_t kHeaderSize = 32;

void putU16(std::vector<uint8_t>& packet, size_t offset, uint16_t value)
{
    const uint16_t networkValue = htons(value);
    std::memcpy(packet.data() + offset, &networkValue, sizeof(networkValue));
}

void putU32(std::vector<uint8_t>& packet, size_t offset, uint32_t value)
{
    const uint32_t networkValue = htonl(value);
    std::memcpy(packet.data() + offset, &networkValue, sizeof(networkValue));
}
}

UdpVideoTransport::UdpVideoTransport(std::string host, uint16_t port,
                                     int jpegQuality, size_t packetSize,
                                     int maxFps)
    : port_(port), jpegQuality_(std::clamp(jpegQuality, 1, 100)),
      packetSize_(std::clamp<size_t>(packetSize, kHeaderSize + 64, 65000)),
      maxFps_(std::max(1, maxFps))
{
    socketFd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (socketFd_ < 0 || inet_pton(AF_INET, host.c_str(), &address_) != 1)
    {
        if (socketFd_ >= 0)
            close(socketFd_);
        socketFd_ = -1;
        return;
    }
    running_ = true;
    worker_ = std::thread(&UdpVideoTransport::run, this);
}

UdpVideoTransport::~UdpVideoTransport()
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

void UdpVideoTransport::publish(uint32_t frameId, uint32_t timestampMs,
                                const cv::Mat& image)
{
    if (socketFd_ < 0 || image.empty())
        return;
    const auto now = std::chrono::steady_clock::now();
    const auto interval = std::chrono::milliseconds(1000 / maxFps_);
    std::lock_guard<std::mutex> lock(mutex_);
    if (lastAccepted_.time_since_epoch().count() != 0 &&
        now - lastAccepted_ < interval)
        return;
    lastAccepted_ = now;
    pendingFrame_ = {frameId, timestampMs, image.clone()};
    hasPendingFrame_ = true;
    condition_.notify_one();
}

void UdpVideoTransport::run()
{
    while (true)
    {
        PendingFrame frame;
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

void UdpVideoTransport::sendFrame(const PendingFrame& frame)
{
    std::vector<uint8_t> jpeg;
    if (!cv::imencode(".jpg", frame.image, jpeg,
                      {cv::IMWRITE_JPEG_QUALITY, jpegQuality_}))
        return;

    const size_t payloadCapacity = packetSize_ - kHeaderSize;
    const size_t chunkCountValue = (jpeg.size() + payloadCapacity - 1) /
                                   payloadCapacity;
    if (chunkCountValue == 0 || chunkCountValue > 65535)
        return;
    const uint16_t chunkCount = static_cast<uint16_t>(chunkCountValue);

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port_);
    destination.sin_addr.s_addr = address_;

    for (uint16_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
    {
        const size_t offset = static_cast<size_t>(chunkIndex) * payloadCapacity;
        const size_t payloadSize = std::min(payloadCapacity, jpeg.size() - offset);
        std::vector<uint8_t> packet(kHeaderSize + payloadSize);
        std::memcpy(packet.data(), "MTV3", 4);
        packet[4] = 1;
        packet[5] = 1;
        putU16(packet, 6, static_cast<uint16_t>(kHeaderSize));
        putU32(packet, 8, frame.frameId);
        putU32(packet, 12, frame.timestampMs);
        putU32(packet, 16, static_cast<uint32_t>(jpeg.size()));
        putU16(packet, 20, chunkIndex);
        putU16(packet, 22, chunkCount);
        putU16(packet, 24, static_cast<uint16_t>(payloadSize));
        putU16(packet, 26, static_cast<uint16_t>(frame.image.cols));
        putU16(packet, 28, static_cast<uint16_t>(frame.image.rows));
        putU16(packet, 30, 0);
        std::memcpy(packet.data() + kHeaderSize, jpeg.data() + offset,
                    payloadSize);
        sendto(socketFd_, packet.data(), packet.size(), MSG_DONTWAIT,
               reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    }
}
