#pragma once

// Lightweight binary UART protocol, pure functions (no I/O) shared by the
// transport, the developer self-tests and tests/uart_protocol.test.cpp.
//   AA 55 | payload length (uint16 LE) | message ID | payload |
//   CRC-16/CCITT-FALSE over ID+payload (uint16 LE) | 55

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "transport/vision_frame.hpp"

namespace binary_uart
{
constexpr size_t kHeaderSize = 5;           // AA 55 len_lo len_hi id
constexpr size_t kOverhead = kHeaderSize + 3;
constexpr size_t kFrameHeaderSize = 14;
constexpr size_t kObjectSize = 14;
constexpr size_t kMaxObjects = 20;
constexpr size_t kMaxPayload = kFrameHeaderSize + kMaxObjects * kObjectSize;

inline uint16_t crc16Ccitt(const uint8_t* data, size_t size)
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

inline void append16(std::vector<uint8_t>& output, uint16_t value)
{
    output.push_back(static_cast<uint8_t>(value));
    output.push_back(static_cast<uint8_t>(value >> 8));
}

inline void append32(std::vector<uint8_t>& output, uint32_t value)
{
    append16(output, static_cast<uint16_t>(value));
    append16(output, static_cast<uint16_t>(value >> 16));
}

inline uint16_t read16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0] | (data[1] << 8));
}

inline uint32_t read32(const uint8_t* data)
{
    return read16(data) | (static_cast<uint32_t>(read16(data + 2)) << 16);
}

inline uint8_t messageId(ProcessingType type)
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

inline uint16_t coordinate(double value)
{
    return static_cast<uint16_t>(std::clamp(std::lround(value), 0l, 65535l));
}

inline std::vector<uint8_t> buildPacket(const VisionFrame& frame, size_t maxObjects)
{
    const size_t count = std::min(frame.objects.size(),
                                  std::clamp<size_t>(maxObjects, 1, kMaxObjects));
    std::vector<uint8_t> payload;
    payload.reserve(kFrameHeaderSize + count * kObjectSize);
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
        append16(payload, coordinate(object.boundingBox.width));
        append16(payload, coordinate(object.boundingBox.height));
        append16(payload, static_cast<uint16_t>(index));
        append16(payload, 0);
    }

    std::vector<uint8_t> packet{0xAA, 0x55};
    packet.reserve(payload.size() + kOverhead);
    append16(packet, static_cast<uint16_t>(payload.size()));
    packet.push_back(messageId(frame.detectorType));
    packet.insert(packet.end(), payload.begin(), payload.end());
    append16(packet, crc16Ccitt(packet.data() + 4, payload.size() + 1));
    packet.push_back(0x55);
    return packet;
}

struct ParsedPacket
{
    std::vector<uint8_t> bytes;     // whole packet as received
    uint8_t messageId = 0;
    bool crcOk = false;
    bool tailOk = false;
    uint32_t frameId = 0;           // valid when payload has the frame header
    uint8_t objects = 0;
};

// Stream parser: resynchronises on AA 55 and rejects impossible lengths, so
// line noise costs at most the bytes it occupies.
class Parser
{
public:
    void feed(const uint8_t* data, size_t size)
    {
        buffer_.insert(buffer_.end(), data, data + size);
        if (buffer_.size() > 4 * (kMaxPayload + kOverhead))
            discard(buffer_.size() - (kMaxPayload + kOverhead));
    }

    bool next(ParsedPacket& packet)
    {
        while (buffer_.size() >= 2)
        {
            size_t start = 0;
            while (start + 1 < buffer_.size() &&
                   !(buffer_[start] == 0xAA && buffer_[start + 1] == 0x55))
                ++start;
            if (start + 1 >= buffer_.size())
            {
                // keep a trailing AA: it may be the first half of a header
                discard(buffer_.back() == 0xAA ? buffer_.size() - 1 : buffer_.size());
                return false;
            }
            discard(start);
            if (buffer_.size() < kHeaderSize)
                return false;
            const size_t length = read16(buffer_.data() + 2);
            if (length > kMaxPayload)
            {
                discard(1);
                continue;
            }
            const size_t total = length + kOverhead;
            if (buffer_.size() < total)
                return false;
            packet = ParsedPacket{};
            packet.bytes.assign(buffer_.begin(), buffer_.begin() + total);
            packet.messageId = packet.bytes[4];
            packet.crcOk = read16(packet.bytes.data() + kHeaderSize + length) ==
                           crc16Ccitt(packet.bytes.data() + 4, length + 1);
            packet.tailOk = packet.bytes.back() == 0x55;
            if (length >= kFrameHeaderSize)
            {
                packet.frameId = read32(packet.bytes.data() + kHeaderSize);
                packet.objects = packet.bytes[kHeaderSize + 12];
            }
            buffer_.erase(buffer_.begin(), buffer_.begin() + total);
            return true;
        }
        return false;
    }

    size_t pending() const { return buffer_.size(); }
    uint64_t discarded() const { return discarded_; }

private:
    void discard(size_t count)
    {
        buffer_.erase(buffer_.begin(), buffer_.begin() + count);
        discarded_ += count;
    }

    std::vector<uint8_t> buffer_;
    uint64_t discarded_ = 0;
};
}
