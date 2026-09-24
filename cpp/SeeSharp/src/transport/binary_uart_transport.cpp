#include "transport/binary_uart_transport.hpp"
#include "transport/binary_uart_protocol.hpp"
#include "transport/uart_rx_log.hpp"
#include "transport/uart_tx_log.hpp"

#include <cerrno>
#include <cstring>

#include <algorithm>
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
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B500000
        case 500000: return B500000;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
#ifdef B1000000
        case 1000000: return B1000000;
#endif
        default: return 0;
    }
}

std::string errnoText(const char* call)
{
    return std::string(call) + ": " + std::strerror(errno);
}
}

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
    if (fd_ >= 0)
    {
        close(fd_);
        UartTxLog::instance().state("Binary TX stopped", device_);
        UartRxLog::instance().state("Binary RX monitor stopped", device_);
    }
}

// Закрывает порт: isOpen() == false, и менеджер удалит транспорт, а не будет
// молча копить кадры без рабочего потока.
bool BinaryUartTransport::fail(const std::string& reason)
{
    if (fd_ >= 0)
        close(fd_);
    fd_ = -1;
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
    const speed_t speed = baudConstant(baud_);
    if (!speed)
        return fail("unsupported baud " + std::to_string(baud_) +
                    " (supported: 9600..115200, 230400, 460800, 500000, 921600, 1000000)");
    // O_RDWR: RX монитор показывает эхо/петлю; протокол сам по себе только TX
    fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY);
    if (fd_ < 0)
        return fail(errnoText("open"));
    termios options{};
    if (tcgetattr(fd_, &options) != 0)
        return fail(errnoText("tcgetattr"));
    cfmakeraw(&options);
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    // cfmakeraw не сбрасывает унаследованные стоп-биты/аппаратный flow control
    options.c_cflag &= ~(CSTOPB | CRTSCTS);
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;          // read() возвращается через 100 мс
    if (tcsetattr(fd_, TCSANOW, &options) != 0)
        return fail(errnoText("tcsetattr"));
    tcflush(fd_, TCIFLUSH);
    std::cout << "UART binary: " << device_ << " @ " << baud_ << std::endl;
    return true;
}

void BinaryUartTransport::publish(const VisionFrame& frame)
{
    if (fd_ < 0)
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
        const ssize_t count = read(fd_, chunk, sizeof(chunk));
        if (count < 0)
        {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            UartRxLog::instance().event("ERROR", "[BIN] " + errnoText("read"));
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
        const ssize_t written = write(fd_, packet.data() + offset,
                                      packet.size() - offset);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
        {
            UartTxLog::instance().count("write_errors");
            UartTxLog::instance().state("Binary TX write failed", device_);
            UartTxLog::instance().event("ERROR", "[BIN] " +
                (written < 0 ? errnoText("write") : std::string("write returned 0")));
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
