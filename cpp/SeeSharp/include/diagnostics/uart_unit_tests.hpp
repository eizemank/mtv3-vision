#pragma once

// Чистые проверки UART-протоколов (без I/O). Каждая функция возвращает ""
// при успехе или текст первой ошибки. Их запускают и режим разработчика
// в web UI (/dev/tests), и tests/uart_protocol.test.cpp на хосте.

#include <chrono>
#include <string>
#include <vector>

#include "transport/binary_uart_protocol.hpp"
#include "transport/dxl_packet_parser.hpp"

namespace uart_unit_tests
{
#define UART_EXPECT(condition, message) \
    do { if (!(condition)) return std::string(message); } while (0)

inline std::string crc16()
{
    const std::string text = "123456789";
    const uint16_t crc = binary_uart::crc16Ccitt(
        reinterpret_cast<const uint8_t*>(text.data()), text.size());
    UART_EXPECT(crc == 0x29B1, "CRC-16/CCITT-FALSE(\"123456789\") = " +
                std::to_string(crc) + ", expected 0x29B1");
    return "";
}

inline VisionFrame sampleFrame(size_t objects)
{
    VisionFrame frame;
    frame.frameId = 0x01020304;
    frame.timestampMs = 123456;
    frame.imageWidth = 640;
    frame.imageHeight = 480;
    frame.detectorType = ProcessingType::BlobDetection;
    for (size_t i = 0; i < objects; ++i)
    {
        BlobMetaData object;
        object.id = static_cast<int>(i + 1);
        object.confidence = 1.0f;
        object.center = cv::Point2f(100.4f + i, 200.6f);
        object.boundingBox = cv::Rect(90, 190, 20 + static_cast<int>(i), 30);
        frame.objects.push_back(object);
    }
    return frame;
}

inline std::string binaryPacket()
{
    const auto packet = binary_uart::buildPacket(sampleFrame(3), 2);
    const size_t payload = binary_uart::kFrameHeaderSize + 2 * binary_uart::kObjectSize;
    UART_EXPECT(packet.size() == payload + binary_uart::kOverhead,
                "packet size " + std::to_string(packet.size()) + ", expected " +
                std::to_string(payload + binary_uart::kOverhead));
    UART_EXPECT(packet[0] == 0xAA && packet[1] == 0x55 && packet.back() == 0x55,
                "wrong AA 55 ... 55 framing");
    UART_EXPECT(binary_uart::read16(packet.data() + 2) == payload, "wrong payload length");
    UART_EXPECT(packet[4] == 0x04, "blob frame must use message ID 0x04");
    const uint8_t* object = packet.data() + binary_uart::kHeaderSize +
                            binary_uart::kFrameHeaderSize;
    UART_EXPECT(object[0] == 1 && object[1] == 255, "object id/confidence mismatch");
    UART_EXPECT(binary_uart::read16(object + 2) == 100 &&
                binary_uart::read16(object + 4) == 201, "object center is not rounded");

    binary_uart::Parser parser;
    parser.feed(packet.data(), packet.size());
    binary_uart::ParsedPacket parsed;
    UART_EXPECT(parser.next(parsed), "parser did not return the packet");
    UART_EXPECT(parsed.crcOk && parsed.tailOk, "CRC or tail check failed");
    UART_EXPECT(parsed.frameId == 0x01020304 && parsed.objects == 2,
                "frame id / object count mismatch after parsing (max_objects=2)");
    return "";
}

inline std::string binaryParserResync()
{
    const auto good = binary_uart::buildPacket(sampleFrame(1), 20);
    auto corrupted = good;
    corrupted[binary_uart::kHeaderSize + 1] ^= 0x40;
    std::vector<uint8_t> stream{0x00, 0x13, 0xAA, 0x37};           // noise
    stream.insert(stream.end(), good.begin(), good.end());
    stream.insert(stream.end(), corrupted.begin(), corrupted.end());
    stream.insert(stream.end(), {0xAA, 0x55, 0xFF, 0xFF, 0x01});    // impossible length
    stream.insert(stream.end(), good.begin(), good.end());

    binary_uart::Parser parser;
    int ok = 0, bad = 0;
    binary_uart::ParsedPacket parsed;
    const size_t split = stream.size() - 7;                         // tail arrives later
    parser.feed(stream.data(), split);
    while (parser.next(parsed)) (parsed.crcOk ? ok : bad)++;
    parser.feed(stream.data() + split, stream.size() - split);
    while (parser.next(parsed)) (parsed.crcOk ? ok : bad)++;
    UART_EXPECT(ok == 2 && bad == 1, "expected 2 valid + 1 CRC error, got " +
                std::to_string(ok) + " + " + std::to_string(bad));
    UART_EXPECT(parser.pending() == 0, "bytes left in the parser after the last packet");
    return "";
}

inline std::string dxlParser()
{
    using namespace std::chrono_literals;
    const auto t0 = DxlPacketParser::Clock::time_point{} + 1s;
    const std::vector<uint8_t> ping{0xFF, 0xFF, 0x01, 0x02, 0x01, 0xFB};
    std::vector<uint8_t> packet;

    DxlPacketParser split;
    split.feed(ping.data(), 3, t0);
    UART_EXPECT(!split.next(packet), "incomplete packet returned");
    split.feed(ping.data() + 3, 3, t0 + 1ms);
    UART_EXPECT(split.next(packet) && packet == ping, "packet split across reads was lost");

    // Регрессия: 1–5 байт шума блокировали push навсегда
    DxlPacketParser noise;
    const std::vector<uint8_t> junk{0x12, 0x34, 0xFF};
    noise.feed(junk.data(), junk.size(), t0);
    noise.next(packet);
    UART_EXPECT(noise.pending() == 1, "noise before a possible header must be dropped");
    UART_EXPECT(noise.expire(t0 + 5ms) == 0, "expired too early");
    UART_EXPECT(noise.expire(t0 + 25ms) == 1 && noise.pending() == 0,
                "stale partial data is not dropped after the timeout");

    // Мусорная длина после FF FF не должна ждать 259 байт
    DxlPacketParser stale;
    const std::vector<uint8_t> header{0xFF, 0xFF, 0x01, 0xF0, 0x02};
    stale.feed(header.data(), header.size(), t0);
    UART_EXPECT(!stale.next(packet), "incomplete long packet returned");
    stale.expire(t0 + 30ms);
    stale.feed(ping.data(), ping.size(), t0 + 31ms);
    UART_EXPECT(stale.next(packet) && packet == ping, "no recovery after an incomplete packet");

    // FF FF FF: заголовок начинается на байт позже; LEN < 2 — не пакет
    DxlPacketParser odd;
    std::vector<uint8_t> stream{0xFF, 0xFF, 0x05, 0x00, 0xFF};
    stream.insert(stream.end(), ping.begin(), ping.end());
    odd.feed(stream.data(), stream.size(), t0);
    UART_EXPECT(odd.next(packet) && packet == ping, "no resync on FF FF FF / LEN < 2");
    UART_EXPECT(odd.pending() == 0, "bytes left after resync");
    return "";
}

#undef UART_EXPECT

struct UnitTest
{
    const char* id;
    const char* title;
    std::string (*run)();
};

inline const std::vector<UnitTest>& all()
{
    static const std::vector<UnitTest> tests{
        {"uart.crc16", "CRC-16/CCITT of the binary protocol", crc16},
        {"uart.binary_packet", "Binary packet layout and max_objects", binaryPacket},
        {"uart.binary_parser", "Binary RX parser: noise, CRC error, split reads", binaryParserResync},
        {"dxl.parser", "DXL parser: split reads, noise timeout, resync", dxlParser},
    };
    return tests;
}
}
