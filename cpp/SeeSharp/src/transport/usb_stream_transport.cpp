#include "transport/usb_stream_transport.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

#include <nlohmann/json.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "platform/socket.hpp"   // htonl
#include "transport/metadata_json.hpp"

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
                                       int maxFps, int maxWidth, bool metadataEnabled,
                                       bool videoEnabled)
    : device_(std::move(device)),
      jpegQuality_(std::clamp(jpegQuality, 1, 100)),
      maxFps_(std::max(1, maxFps)),
      maxWidth_(std::clamp(maxWidth, 160, 1920)),
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
    const auto now = std::chrono::steady_clock::now();
    if (now < nextFrame_)
        return;
    nextFrame_ = now + std::chrono::milliseconds(1000 / maxFps_);
    pendingFrame_.metadata = frame;
    pendingFrame_.image = videoEnabled_ ? image.clone() : cv::Mat();
    hasPendingFrame_ = true;
    condition_.notify_one();
}

bool UsbStreamTransport::openDevice()
{
    if (port_.isOpen())
        return true;
    // только запись, неблокирующая; скорость USB CDC не трогаем (baud 0)
    std::string error;
    if (!port_.open(device_, 0, true, 0, error))
        return false;
    std::cout << "USB stream: " << device_ << std::endl;
    return true;
}

void UsbStreamTransport::closeDevice()
{
    port_.close();
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
        const long written = port_.write(record.data() + offset,
                                         record.size() - offset);
        if (written > 0)
            offset += static_cast<size_t>(written);
        else if (written == 0)
            return false;                 // устройство не готово (хост не читает)
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
    return serializeVisionFrameJson(frame, kMaxMetadataObjects).dump();
}

void UsbStreamTransport::run()
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
        if (metadataEnabled_)
        {
            const std::string json = serializeMetadata(frame.metadata);
            writeRecord(kMetadataMessage, frame.metadata.frameId,
                        reinterpret_cast<const uint8_t*>(json.data()), json.size());
        }
        if (videoEnabled_ && !frame.image.empty())
        {
            cv::Mat encodedImage = frame.image;
            if (frame.image.cols > maxWidth_)
            {
                const double scale = static_cast<double>(maxWidth_) / frame.image.cols;
                cv::resize(frame.image, encodedImage,
                           {maxWidth_, std::max(1, cvRound(frame.image.rows * scale))},
                           0.0, 0.0, cv::INTER_AREA);
            }
            std::vector<uint8_t> jpeg;
            if (cv::imencode(".jpg", encodedImage, jpeg,
                             {cv::IMWRITE_JPEG_QUALITY, jpegQuality_}))
                writeRecord(kJpegMessage, frame.metadata.frameId,
                            jpeg.data(), jpeg.size());
        }
    }
}
