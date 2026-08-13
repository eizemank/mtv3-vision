#include "transport/udp_metadata_transport.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

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

    nlohmann::json detections = nlohmann::json::array();
    constexpr size_t kMaxUdpObjects = 200;
    const size_t objectCount = std::min(frame.objects.size(), kMaxUdpObjects);
    for (size_t objectIndex = 0; objectIndex < objectCount; ++objectIndex)
    {
        const BlobMetaData& object = frame.objects[objectIndex];
        const float width = std::max(1, static_cast<int>(frame.imageWidth));
        const float height = std::max(1, static_cast<int>(frame.imageHeight));
        detections.push_back({
            {"class_id", object.id},
            {"confidence", (frame.detectorType == ProcessingType::Classification ||
                             frame.detectorType == ProcessingType::ObjectDetection)
                                   ? object.area : 1.0},
            {"center", {object.center.x / width, object.center.y / height}},
            {"bbox", {
                {"x", (object.boundingBox.x + object.boundingBox.width * 0.5) / width},
                {"y", (object.boundingBox.y + object.boundingBox.height * 0.5) / height},
                {"w", object.boundingBox.width / width},
                {"h", object.boundingBox.height / height}
            }},
            {"area", object.area}
        });
    }

    nlohmann::json message = {
        {"version", "1.0"},
        {"msg_type", "detection_frame"},
        {"frame_id", frame.frameId},
        {"timestamp_ms", frame.timestampMs},
        {"image_size", {frame.imageWidth, frame.imageHeight}},
        {"detector", detectorTypeName(frame.detectorType)},
        {"inference_ms", frame.inferenceUs / 1000.0},
        {"fps", frame.fps},
        {"truncated", frame.objects.size() > objectCount},
        {"detections", std::move(detections)}
    };
    const std::string payload = message.dump();

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port_);
    destination.sin_addr.s_addr = address_;
    sendto(socketFd_, payload.data(), payload.size(), MSG_DONTWAIT,
           reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
}
