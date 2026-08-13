#include "transport/usb_stream_transport.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>

namespace
{
constexpr size_t kHeaderSize = 16;
constexpr uint8_t kMetadataMessage = 1;
constexpr uint8_t kJpegMessage = 2;
constexpr size_t kMaxMetadataObjects = 200;

void putU32(uint8_t* destination, uint32_t value)
{
    const uint32_t networkValue = htonl(value);
    std::memcpy(destination, &networkValue, sizeof(networkValue));
}
}

UsbStreamTransport::UsbStreamTransport(std::string device, int jpegQuality,
                                       int maxFps, bool metadataEnabled,
                                       bool videoEnabled)
    : device_(std::move(device)),
      jpegQuality_(std::clamp(jpegQuality, 1, 100)),
      maxFps_(std::max(1, maxFps)),
      metadataEnabled_(metadataEnabled), videoEnabled_(videoEnabled),
      worker_(&UsbStreamTransport::run, this)
{
}

UsbStreamTransport::~UsbStreamTransport()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
    closeDevice();
}

void UsbStreamTransport::publish(const VisionFrame& frame, const cv::Mat& image)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFrame_.metadata = frame;
    pendingFrame_.image = videoEnabled_ ? image.clone() : cv::Mat();
    hasPendingFrame_ = true;
    condition_.notify_one();
}

bool UsbStreamTransport::openDevice()
{
    if (deviceFd_ >= 0)
        return true;
    deviceFd_ = open(device_.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK);
    if (deviceFd_ < 0)
        return false;
    termios tty{};
    if (tcgetattr(deviceFd_, &tty) == 0)
    {
        cfmakeraw(&tty);
        tcsetattr(deviceFd_, TCSANOW, &tty);
    }
    std::cout << "USB stream: " << device_ << std::endl;
    return true;
}

void UsbStreamTransport::closeDevice()
{
    if (deviceFd_ >= 0)
        close(deviceFd_);
    deviceFd_ = -1;
}

bool UsbStreamTransport::writeRecord(uint8_t messageType, uint32_t frameId,
                                     const uint8_t* data, size_t size)
{
    if (size > UINT32_MAX || !openDevice())
        return false;
    std::vector<uint8_t> record(kHeaderSize + size);
    std::memcpy(record.data(), "MTVU", 4);
    record[4] = 1;
    record[5] = messageType;
    record[6] = 0;
    record[7] = 0;
    putU32(record.data() + 8, frameId);
    putU32(record.data() + 12, static_cast<uint32_t>(size));
    if (size > 0)
        std::memcpy(record.data() + kHeaderSize, data, size);

    size_t offset = 0;
    while (offset < record.size())
    {
        const ssize_t written = write(deviceFd_, record.data() + offset,
                                      record.size() - offset);
        if (written > 0)
            offset += static_cast<size_t>(written);
        else if (written < 0 && errno == EINTR)
            continue;
        else if (written < 0 && errno == EAGAIN)
        {
            pollfd descriptor{deviceFd_, POLLOUT, 0};
            if (poll(&descriptor, 1, 100) > 0)
                continue;
            return false;
        }
        else
        {
            closeDevice();
            return false;
        }
    }
    return true;
}

std::string UsbStreamTransport::serializeMetadata(const VisionFrame& frame) const
{
    nlohmann::json objects = nlohmann::json::array();
    const size_t count = std::min(frame.objects.size(), kMaxMetadataObjects);
    const float width = std::max(1, static_cast<int>(frame.imageWidth));
    const float height = std::max(1, static_cast<int>(frame.imageHeight));
    for (size_t index = 0; index < count; ++index)
    {
        const BlobMetaData& object = frame.objects[index];
        objects.push_back({
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
    return nlohmann::json({
        {"version", "1.0"}, {"msg_type", "detection_frame"},
        {"frame_id", frame.frameId}, {"timestamp_ms", frame.timestampMs},
        {"image_size", {frame.imageWidth, frame.imageHeight}},
        {"detector", detectorTypeName(frame.detectorType)},
        {"inference_ms", frame.inferenceUs / 1000.0}, {"fps", frame.fps},
        {"truncated", frame.objects.size() > count},
        {"detections", std::move(objects)}
    }).dump();
}

void UsbStreamTransport::run()
{
    const auto minimumInterval = std::chrono::milliseconds(1000 / maxFps_);
    auto nextFrame = std::chrono::steady_clock::now();
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
        const auto now = std::chrono::steady_clock::now();
        if (now < nextFrame)
            continue;
        nextFrame = now + minimumInterval;

        if (metadataEnabled_)
        {
            const std::string json = serializeMetadata(frame.metadata);
            writeRecord(kMetadataMessage, frame.metadata.frameId,
                        reinterpret_cast<const uint8_t*>(json.data()), json.size());
        }
        if (videoEnabled_ && !frame.image.empty())
        {
            std::vector<uint8_t> jpeg;
            if (cv::imencode(".jpg", frame.image, jpeg,
                             {cv::IMWRITE_JPEG_QUALITY, jpegQuality_}))
                writeRecord(kJpegMessage, frame.metadata.frameId,
                            jpeg.data(), jpeg.size());
        }
    }
}
