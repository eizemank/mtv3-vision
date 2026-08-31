#include "transport/binary_uart_transport.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <termios.h>
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

namespace
{
speed_t baudConstant(int baud)
{
    switch (baud)
    {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
        default: return 0;
    }
}

void append16(std::vector<uint8_t>& output, uint16_t value)
{
    output.push_back(static_cast<uint8_t>(value));
    output.push_back(static_cast<uint8_t>(value >> 8));
}

void append32(std::vector<uint8_t>& output, uint32_t value)
{
    append16(output, static_cast<uint16_t>(value));
    append16(output, static_cast<uint16_t>(value >> 16));
}

uint16_t crc16Ccitt(const uint8_t* data, size_t size)
{
    uint16_t crc = 0xFFFF;
    for (size_t index = 0; index < size; ++index)
    {
        crc ^= static_cast<uint16_t>(data[index]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

uint8_t messageId(ProcessingType type)
{
    switch (type)
    {
        case ProcessingType::Classification:
        case ProcessingType::ObjectDetection: return 0x02;
        case ProcessingType::ArucoDetection: return 0x03;
        case ProcessingType::BlobDetection: return 0x04;
        case ProcessingType::LineDetection: return 0x05;
        case ProcessingType::CircleDetection: return 0x06;
        default: return 0x01;
    }
}

uint16_t coordinate(float value)
{
    return static_cast<uint16_t>(std::clamp(std::lround(value), 0l, 65535l));
}
}

BinaryUartTransport::BinaryUartTransport(std::string device, int baud,
                                         size_t maxObjects)
    : device_(std::move(device)), baud_(baud),
      maxObjects_(std::clamp<size_t>(maxObjects, 1, 20))
{
    if (openPort())
        worker_ = std::thread(&BinaryUartTransport::run, this);
}

BinaryUartTransport::~BinaryUartTransport()
{
    running_ = false;
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (fd_ >= 0)
        close(fd_);
}

bool BinaryUartTransport::openPort()
{
    const speed_t speed = baudConstant(baud_);
    if (!speed)
        return false;
    fd_ = open(device_.c_str(), O_WRONLY | O_NOCTTY);
    if (fd_ < 0)
        return false;
    termios options{};
    if (tcgetattr(fd_, &options) != 0)
        return false;
    cfmakeraw(&options);
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    options.c_cflag |= CLOCAL | CREAD;
    if (tcsetattr(fd_, TCSANOW, &options) != 0)
        return false;
    std::cout << "UART binary: " << device_ << " @ " << baud_ << std::endl;
    return true;
}

void BinaryUartTransport::publish(const VisionFrame& frame)
{
    if (fd_ < 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFrame_ = frame;
    hasFrame_ = true;
    ready_.notify_one();
}

void BinaryUartTransport::run()
{
    while (running_)
    {
        VisionFrame frame;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return hasFrame_ || !running_; });
            if (!running_)
                break;
            frame = pendingFrame_;
            hasFrame_ = false;
        }
        sendFrame(frame);
    }
}

void BinaryUartTransport::sendFrame(const VisionFrame& frame)
{
    const size_t count = std::min(frame.objects.size(), maxObjects_);
    std::vector<uint8_t> payload;
    payload.reserve(14 + count * 14);
    append32(payload, frame.frameId);
    append32(payload, frame.timestampMs);
    append16(payload, frame.imageWidth);
    append16(payload, frame.imageHeight);
    payload.push_back(static_cast<uint8_t>(count));
    payload.push_back(0);
    for (size_t index = 0; index < count; ++index)
    {
        const BlobMetaData& object = frame.objects[index];
        payload.push_back(static_cast<uint8_t>(std::clamp(object.id, 0, 255)));
        payload.push_back(static_cast<uint8_t>(std::clamp(
            std::lround(object.confidence * 255.0), 0l, 255l)));
        append16(payload, coordinate(object.center.x));
        append16(payload, coordinate(object.center.y));
        append16(payload, coordinate(static_cast<float>(object.boundingBox.width)));
        append16(payload, coordinate(static_cast<float>(object.boundingBox.height)));
        append16(payload, static_cast<uint16_t>(index));
        append16(payload, 0);
    }

    const uint8_t id = messageId(frame.detectorType);
    std::vector<uint8_t> packet{0xAA, 0x55};
    append16(packet, static_cast<uint16_t>(payload.size()));
    packet.push_back(id);
    packet.insert(packet.end(), payload.begin(), payload.end());
    const uint16_t crc = crc16Ccitt(packet.data() + 4, payload.size() + 1);
    append16(packet, crc);
    packet.push_back(0x55);

    size_t offset = 0;
    while (offset < packet.size())
    {
        const ssize_t written = write(fd_, packet.data() + offset,
                                      packet.size() - offset);
        if (written <= 0)
            return;
        offset += static_cast<size_t>(written);
    }
}
