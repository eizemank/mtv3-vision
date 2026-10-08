#include "transport/binary_uart_transport.hpp"
#include "transport/binary_uart_protocol.hpp"
#include "transport/uart_rx_log.hpp"
#include "transport/uart_tx_log.hpp"

#include <cerrno>
#include <cstring>

#include <algorithm>
#include <iostream>
#include <utility>
#include <vector>

BinaryUartTransport::BinaryUartTransport(std::string device, int baud,
                                         size_t maxObjects)
    : device_(std::move(device)), baud_(baud),
      maxObjects_(std::clamp<size_t>(maxObjects, 1, binary_uart::kMaxObjects))
{
    UartTxLog::instance().state("Opening binary UART @ " + std::to_string(baud_), device_);
    if (openPort())
    {
        UartTxLog::instance().state("Binary TX active @ " + std::to_string(baud_), device_);
        UartRxLog::instance().state("Binary RX monitor active (loopback/echo check)", device_);
        worker_ = std::thread(&BinaryUartTransport::run, this);
        rxMonitor_ = std::thread(&BinaryUartTransport::monitorRx, this);
    }
}

BinaryUartTransport::~BinaryUartTransport()
{
    running_ = false;
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (rxMonitor_.joinable())
        rxMonitor_.join();
    if (port_.isOpen())
    {
        port_.close();
        UartTxLog::instance().state("Binary TX stopped", device_);
        UartRxLog::instance().state("Binary RX monitor stopped", device_);
    }
}

// Закрывает порт: isOpen() == false, и менеджер удалит транспорт, а не будет
// молча копить кадры без рабочего потока.
bool BinaryUartTransport::fail(const std::string& reason)
{
    port_.close();
    const std::string message = "Binary UART " + device_ + ": " + reason;
    std::cerr << message << std::endl;
    UartTxLog::instance().state("Failed to open/configure binary UART", device_);
    UartTxLog::instance().event("ERROR", message);
    UartRxLog::instance().state("Binary RX monitor off: port is not open", device_);
    UartRxLog::instance().event("ERROR", message);
    return false;
}

bool BinaryUartTransport::openPort()
{
    if (!platform::SerialPort::baudSupported(baud_))
        return fail("unsupported baud " + std::to_string(baud_) +
                    " (supported: " + platform::SerialPort::supportedBauds() + ")");
    // чтение+запись: RX монитор показывает эхо/петлю; протокол сам по себе
    // только TX. read() возвращается через 100 мс без данных.
    std::string error;
    if (!port_.open(device_, baud_, false, 100, error))
        return fail(error);
    std::cout << "UART binary: " << device_ << " @ " << baud_ << std::endl;
    return true;
}

void BinaryUartTransport::publish(const VisionFrame& frame)
{
    if (!port_.isOpen())
        return;
    UartTxLog::instance().count("frames_published");
    std::lock_guard<std::mutex> lock(mutex_);
    // UART медленнее кадров: неотправленный кадр заменяется свежим
    if (hasFrame_)
        UartTxLog::instance().count("frames_dropped_busy");
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

void BinaryUartTransport::monitorRx()
{
    binary_uart::Parser parser;
    uint64_t reportedDiscarded = 0;
    while (running_)
    {
        uint8_t chunk[256];
        const long count = port_.read(chunk, sizeof(chunk));
        if (count < 0)
        {
            UartRxLog::instance().event("ERROR", "[BIN] " + port_.lastError());
            UartRxLog::instance().state("Binary RX monitor failed", device_);
            return;
        }
        if (count == 0)
            continue;
        UartRxLog::instance().record(chunk, static_cast<size_t>(count), false,
                                     "[BIN] Raw RX chunk");
        parser.feed(chunk, static_cast<size_t>(count));
        binary_uart::ParsedPacket packet;
        while (parser.next(packet))
        {
            const bool ok = packet.crcOk && packet.tailOk;
            UartRxLog::instance().count(ok ? "rx_frames_ok" : "rx_frames_bad");
            UartRxLog::instance().record(packet.bytes.data(), packet.bytes.size(), true,
                "[BIN] message=" + std::to_string(packet.messageId) +
                " frame=" + std::to_string(packet.frameId) +
                " objects=" + std::to_string(packet.objects) +
                (packet.crcOk ? " crc=OK" : " crc=ERROR") +
                (packet.tailOk ? "" : " tail=ERROR"));
        }
        if (parser.discarded() != reportedDiscarded)
        {
            UartRxLog::instance().count("rx_discarded_bytes",
                                        parser.discarded() - reportedDiscarded);
            reportedDiscarded = parser.discarded();
        }
    }
}

void BinaryUartTransport::sendFrame(const VisionFrame& frame)
{
    const std::vector<uint8_t> packet = binary_uart::buildPacket(frame, maxObjects_);
    size_t offset = 0;
    while (offset < packet.size())
    {
        const long written = port_.write(packet.data() + offset,
                                         packet.size() - offset);
        if (written <= 0)
        {
            UartTxLog::instance().count("write_errors");
            UartTxLog::instance().state("Binary TX write failed", device_);
            UartTxLog::instance().event("ERROR", "[BIN] " +
                (written < 0 ? port_.lastError() : std::string("write timed out")));
            return;
        }
        UartTxLog::instance().record(packet.data() + offset,
            static_cast<size_t>(written), false, "[BIN] Raw TX chunk");
        offset += static_cast<size_t>(written);
    }
    UartTxLog::instance().count("frames_sent");
    UartTxLog::instance().state("Binary TX active @ " + std::to_string(baud_), device_);
    UartTxLog::instance().record(packet.data(), packet.size(), true,
        "[BIN] message=" + std::to_string(packet[4]) +
        " frame=" + std::to_string(frame.frameId) +
        " objects=" + std::to_string(packet[binary_uart::kHeaderSize + 12]));
}
