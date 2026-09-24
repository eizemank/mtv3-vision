#include "transport/dxl_uart_transport.hpp"
#include "transport/dxl_packet_parser.hpp"
#include "transport/uart_rx_log.hpp"
#include "transport/uart_tx_log.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

#include <fcntl.h>
#include <linux/serial.h>
#include <sys/ioctl.h>
#include <sys/sysinfo.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

namespace
{
constexpr uint8_t kBroadcastId = 0xFE;
constexpr uint8_t kErrorRange = 0x08;
constexpr uint8_t kErrorChecksum = 0x10;
constexpr uint8_t kErrorInstruction = 0x40;

uint8_t checksum(const uint8_t* data, size_t size)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < size; ++i)
        sum = static_cast<uint8_t>(sum + data[i]);
    return static_cast<uint8_t>(~sum);
}

template <typename T>
T clampCast(double value)
{
    return static_cast<T>(std::clamp(
        value, static_cast<double>(std::numeric_limits<T>::min()),
        static_cast<double>(std::numeric_limits<T>::max())));
}

void putU16(std::array<uint8_t, 256>& data, size_t offset, uint16_t value)
{
    data[offset] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void putI16(std::array<uint8_t, 256>& data, size_t offset, int16_t value)
{
    putU16(data, offset, static_cast<uint16_t>(value));
}

void putU32(std::array<uint8_t, 256>& data, size_t offset, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        data[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

speed_t baudConstant(int baud)
{
    switch (baud)
    {
        case 9600: return B9600;
        case 19200: return B19200;
        case 57600: return B57600;
        case 115200: return B115200;
#ifdef B200000
        case 200000: return B200000;
#endif
        case 230400: return B230400;
#ifdef B250000
        case 250000: return B250000;
#endif
#ifdef B400000
        case 400000: return B400000;
#endif
        case 500000: return B500000;
        case 1000000: return B1000000;
        default: return 0;
    }
}

uint8_t baudIndex(int baud)
{
    const int index = std::max(0, 2000000 / std::max(1, baud) - 1);
    return clampCast<uint8_t>(index);
}

std::string errnoText(const char* call)
{
    return std::string(call) + ": " + std::strerror(errno);
}

void dxlError(const std::string& message)
{
    std::cerr << message << std::endl;
    UartRxLog::instance().event("ERROR", message);
    UartTxLog::instance().event("ERROR", message);
}

int baudFromIndex(uint8_t index)
{
    switch (index)
    {
        case 1: return 1000000;
        case 3: return 500000;
        case 4: return 400000;
        case 7: return 250000;
        case 9: return 200000;
        case 16: return 115200;
        case 34: return 57600;
        case 103: return 19200;
        case 207: return 9600;
        default: return 115200;
    }
}
}

DxlUartTransport::DxlUartTransport(std::string device, int baud,
                                   uint8_t deviceId, bool rs485,
                                   std::string eepromPath,
                                   DetectorCallback detectorCallback,
                                   bool startupPush, uint8_t pushIntervalMs)
    : device_(std::move(device)), baud_(baud), rs485_(rs485),
      eepromPath_(std::move(eepromPath)),
      detectorCallback_(std::move(detectorCallback))
{
    initializeControlTable(deviceId, baud);
    table_[0x13] = startupPush ? 1 : 0;
    table_[0x14] = std::max<uint8_t>(1, pushIntervalMs);
    loadEeprom();
    baud_ = baudFromIndex(table_[0x04]);
    // EEPROM (и таблица DXL в целом) главнее config.json: контроллер мог
    // сменить скорость записью в 0x04, и она переживает перезапуск
    if (baud_ != baud)
    {
        const std::string warning = "[DXL] baud " + std::to_string(baud_) +
            " from " + eepromPath_ + " / DXL table overrides config baud " +
            std::to_string(baud);
        std::cerr << warning << std::endl;
        UartRxLog::instance().event("WARN", warning);
        UartTxLog::instance().event("WARN", warning);
    }
    const std::string at = " @ " + std::to_string(baud_) +
                           " ID=" + std::to_string(table_[0x03]);
    UartRxLog::instance().state("Opening DXL UART" + at, device_);
    UartTxLog::instance().state("Opening DXL UART" + at, device_);
    if (openPort())
    {
        UartRxLog::instance().state("DXL RX active" + at, device_);
        UartTxLog::instance().state("DXL TX active" + at, device_);
        worker_ = std::thread(&DxlUartTransport::run, this);
    }
    else
    {
        UartRxLog::instance().state("Failed to open/configure DXL UART", device_);
        UartTxLog::instance().state("Failed to open/configure DXL UART", device_);
    }
}

void DxlUartTransport::loadEeprom()
{
    std::ifstream input(eepromPath_, std::ios::binary);
    if (input)
        input.read(reinterpret_cast<char*>(table_.data() + 0x03), 5);
}

void DxlUartTransport::saveEepromLocked()
{
    const std::string temporary = eepromPath_ + ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
        return;
    output.write(reinterpret_cast<const char*>(table_.data() + 0x03), 5);
    output.close();
    std::rename(temporary.c_str(), eepromPath_.c_str());
}

DxlUartTransport::~DxlUartTransport()
{
    running_ = false;
    if (worker_.joinable())
        worker_.join();
    if (serialFd_ >= 0)
    {
        close(serialFd_);
        UartRxLog::instance().state("DXL RX stopped", device_);
        UartTxLog::instance().state("DXL TX stopped", device_);
    }
}

void DxlUartTransport::initializeControlTable(uint8_t deviceId, int baud)
{
    table_.fill(0);
    putU16(table_, 0x00, 0x5643);
    table_[0x02] = 0x20;
    table_[0x03] = deviceId;
    table_[0x04] = baudIndex(baud);
    table_[0x05] = 250;
    table_[0x06] = 2;
    table_[0x11] = 128;
    table_[0x12] = 10;
    table_[0x14] = 33;
    table_[0x17] = 255;
    table_[0x18] = 255;
    table_[0x1A] = 4;
    table_[0x1B] = 0xFF;
    table_[0x1C] = 0xFF;
    table_[0x20] = 1;
    table_[0x21] = 1;
}

bool DxlUartTransport::openPort()
{
    const speed_t speed = baudConstant(baud_);
    if (!speed)
    {
        dxlError("[DXL] " + device_ + ": baud " + std::to_string(baud_) +
                 " is not supported by this kernel/libc");
        return false;
    }
    serialFd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serialFd_ < 0)
    {
        dxlError("[DXL] " + device_ + ": " + errnoText("open"));
        return false;
    }

    termios tty{};
    if (tcgetattr(serialFd_, &tty) != 0)
    {
        dxlError("[DXL] " + device_ + ": " + errnoText("tcgetattr"));
        close(serialFd_);
        serialFd_ = -1;
        return false;
    }
    cfmakeraw(&tty);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    // cfmakeraw does not clear inherited stop-bit/hardware-flow settings.
    tty.c_cflag &= ~(CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;
    if (tcsetattr(serialFd_, TCSANOW, &tty) != 0)
    {
        dxlError("[DXL] " + device_ + ": " + errnoText("tcsetattr"));
        close(serialFd_);
        serialFd_ = -1;
        return false;
    }

    if (rs485_)
    {
        serial_rs485 config{};
        config.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
        if (ioctl(serialFd_, TIOCSRS485, &config) != 0)
        {
            const std::string warning = "[DXL] RS-485 direction ioctl is not supported: " +
                                        errnoText("ioctl(TIOCSRS485)");
            std::cerr << warning << std::endl;
            UartTxLog::instance().event("WARN", warning);
        }
    }
    std::cout << "UART DXL 1.0: " << device_ << " @ " << baud_ << std::endl;
    return true;
}

void DxlUartTransport::publish(const VisionFrame& frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    table_[0x20] = 1;
    table_[0x21] = 2;
    table_[0x22] = clampCast<uint8_t>(std::round(frame.fps));
    table_[0x26] = detectorTypeCode(frame.detectorType);
    table_[0x10] = detectorTypeCode(frame.detectorType);
    table_[0x27] |= 0x10;
    putU16(table_, 0x2E, static_cast<uint16_t>(frame.frameId));
    if (frame.frameId % 30 == 0)
    {
        struct sysinfo info{};
        if (sysinfo(&info) == 0)
        {
            putU32(table_, 0x28, clampCast<uint32_t>(info.uptime));
            const uint64_t usedBytes =
                static_cast<uint64_t>(info.totalram - info.freeram) * info.mem_unit;
            putU16(table_, 0x2C, clampCast<uint16_t>(usedBytes / (1024 * 1024)));
        }
        std::ifstream temperature("/sys/class/thermal/thermal_zone0/temp");
        int millidegrees = 0;
        if (temperature >> millidegrees)
            table_[0x23] = clampCast<uint8_t>(millidegrees / 1000);
    }
    putU32(table_, 0x30, frame.frameId);
    putU32(table_, 0x34, frame.timestampMs);
    table_[0x39] = detectorTypeCode(frame.detectorType);
    putU16(table_, 0x3A, clampCast<uint16_t>(frame.inferenceUs / 10));

    std::fill(table_.begin() + 0x3C, table_.end(), 0);
    const uint8_t type = table_[0x39];
    const size_t objectSize = type == 2 ? 30 : type == 1 ? 14 :
                              type == 5 ? 8 : 12;
    const size_t tableCapacity = (table_.size() - 0x3C) / objectSize;
    const uint8_t maxObjects = static_cast<uint8_t>(std::min<size_t>(
        std::clamp<uint8_t>(table_[0x12], 1, 15), tableCapacity));
    const size_t count = std::min<size_t>(frame.objects.size(), maxObjects);
    table_[0x38] = static_cast<uint8_t>(count);
    if (frame.objects.size() > count)
        table_[0x27] |= 0x08;
    else
        table_[0x27] &= static_cast<uint8_t>(~0x08);

    for (size_t i = 0; i < count; ++i)
    {
        const BlobMetaData& object = frame.objects[i];
        const size_t offset = 0x3C + i * objectSize;
        if (offset + objectSize > table_.size())
            break;
        const uint16_t cx = clampCast<uint16_t>(object.center.x);
        const uint16_t cy = clampCast<uint16_t>(object.center.y);

        if (type == 1)
        {
            table_[offset] = clampCast<uint8_t>(object.id);
            table_[offset + 1] = clampCast<uint8_t>(object.area * 255.0);
            putU16(table_, offset + 2, cx);
            putU16(table_, offset + 4, cy);
            putU16(table_, offset + 6, clampCast<uint16_t>(object.boundingBox.width));
            putU16(table_, offset + 8, clampCast<uint16_t>(object.boundingBox.height));
        }
        else if (type == 2)
        {
            putU16(table_, offset, clampCast<uint16_t>(object.id));
            const int16_t left = clampCast<int16_t>(object.boundingBox.x);
            const int16_t top = clampCast<int16_t>(object.boundingBox.y);
            const int16_t right = clampCast<int16_t>(object.boundingBox.x + object.boundingBox.width);
            const int16_t bottom = clampCast<int16_t>(object.boundingBox.y + object.boundingBox.height);
            const int16_t corners[8] = {left, top, right, top, right, bottom, left, bottom};
            for (size_t corner = 0; corner < 8; ++corner)
                putI16(table_, offset + 2 + corner * 2, corners[corner]);
        }
        else if (type == 3)
        {
            putU16(table_, offset, clampCast<uint16_t>(object.center.x * 10));
            putU16(table_, offset + 2, clampCast<uint16_t>(object.center.y * 10));
            putU16(table_, offset + 4, clampCast<uint16_t>(object.area));
            putU16(table_, offset + 6, clampCast<uint16_t>(
                std::max(object.boundingBox.width, object.boundingBox.height) * 10));
        }
        else if (type == 4)
        {
            const int16_t x1 = clampCast<int16_t>(object.boundingBox.x);
            const int16_t y1 = clampCast<int16_t>(object.boundingBox.y);
            const int16_t x2 = clampCast<int16_t>(object.boundingBox.x + object.boundingBox.width);
            const int16_t y2 = clampCast<int16_t>(object.boundingBox.y + object.boundingBox.height);
            putI16(table_, offset, x1);
            putI16(table_, offset + 2, y1);
            putI16(table_, offset + 4, x2);
            putI16(table_, offset + 6, y2);
            putI16(table_, offset + 8, clampCast<int16_t>(
                std::atan2(y2 - y1, x2 - x1) * 18000.0 / 3.141592653589793));
            table_[offset + 10] = 255;
        }
        else if (type == 5)
        {
            putU16(table_, offset, clampCast<uint16_t>(object.center.x * 10));
            putU16(table_, offset + 2, clampCast<uint16_t>(object.center.y * 10));
            putU16(table_, offset + 4, clampCast<uint16_t>(
                object.boundingBox.width * 5));
        }
    }
    frameReady_ = true;
}

bool DxlUartTransport::writeAll(const std::vector<uint8_t>& packet)
{
    lastTransmit_ = packet;
    size_t offset = 0;
    while (offset < packet.size())
    {
        const ssize_t written = write(serialFd_, packet.data() + offset,
                                      packet.size() - offset);
        if (written > 0)
        {
            UartTxLog::instance().record(packet.data() + offset,
                static_cast<size_t>(written), false, "[DXL] Raw TX chunk");
            offset += static_cast<size_t>(written);
        }
        else if (written < 0 && (errno == EAGAIN || errno == EINTR))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        else
        {
            UartTxLog::instance().count("write_errors");
            UartTxLog::instance().state("DXL TX write failed", device_);
            UartTxLog::instance().event("ERROR", "[DXL] " +
                (written < 0 ? errnoText("write") : std::string("write returned 0")));
            return false;
        }
    }
    UartTxLog::instance().state("DXL TX active @ " + std::to_string(baud_) +
                                " ID=" + std::to_string(packet[2]), device_);
    UartTxLog::instance().record(packet.data(), packet.size(), true,
        "[DXL] STATUS ID=" + std::to_string(packet[2]) +
        " error=" + std::to_string(packet[4]) +
        " params=" + std::to_string(packet.size() - 6));
    tcdrain(serialFd_);
    return true;
}

bool DxlUartTransport::sendStatus(uint8_t error,
                                  const std::vector<uint8_t>& params)
{
    uint8_t id;
    uint8_t delay;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        id = table_[0x03];
        delay = table_[0x05];
    }
    std::vector<uint8_t> packet{0xFF, 0xFF, id,
                                static_cast<uint8_t>(params.size() + 2), error};
    packet.insert(packet.end(), params.begin(), params.end());
    packet.push_back(checksum(packet.data() + 2, packet.size() - 2));
    std::this_thread::sleep_for(std::chrono::microseconds(delay * 2));
    return writeAll(packet);
}

void DxlUartTransport::applyWrite(uint8_t address,
                                  const std::vector<uint8_t>& data)
{
    uint8_t requestedDetector = 0;
    bool detectorChanged = false;
    bool eepromChanged = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < data.size() && address + i < table_.size(); ++i)
        {
            const size_t target = address + i;
            const bool writable = (target >= 0x03 && target <= 0x07) ||
                                  (target >= 0x10 && target <= 0x1D);
            if (writable)
            {
                table_[target] = data[i];
                eepromChanged = eepromChanged || target <= 0x07;
            }
        }
        table_[0x12] = std::clamp<uint8_t>(table_[0x12], 1, 15);
        if (address <= 0x10 && address + data.size() > 0x10)
        {
            requestedDetector = table_[0x10];
            detectorChanged = true;
        }
        if (eepromChanged)
            saveEepromLocked();
    }
    if (detectorChanged && detectorCallback_ && !detectorCallback_(requestedDetector))
    {
        std::lock_guard<std::mutex> lock(mutex_);
        table_[0x20] = 2;
        ++table_[0x25];
    }
}

void DxlUartTransport::processPacket(const std::vector<uint8_t>& packet)
{
    if (packet.size() < 6)
        return;
    const bool validChecksum = packet.back() == checksum(packet.data() + 2, packet.size() - 3);
    const char* command = "UNKNOWN";
    switch (packet[4]) {
        case 1: command = "PING"; break;
        case 2: command = "READ"; break;
        case 3: command = "WRITE"; break;
        case 4: command = "REG_WRITE"; break;
        case 5: command = "ACTION"; break;
        case 6: command = "FACTORY_RESET"; break;
        case 8: command = "REBOOT"; break;
    }
    std::string detail = "ID=" + std::to_string(packet[2]) + " " + command;
    if ((packet[4] == 2 || packet[4] == 3 || packet[4] == 4) && packet.size() >= 8)
        detail += " address=" + std::to_string(packet[5]) +
            (packet[4] == 2 ? " length=" : " value[0]=") + std::to_string(packet[6]);
    detail += validChecksum ? " checksum=OK" : " checksum=ERROR";
    const char* counter = validChecksum ? "rx_packets_ok" : "rx_checksum_errors";
    if (packet == lastTransmit_)
    {
        detail += " echo ignored";
        counter = "rx_echo";
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (packet[2] != table_[0x03] && packet[2] != kBroadcastId)
        {
            detail += " foreign ID ignored (own ID=" + std::to_string(table_[0x03]) + ")";
            counter = "rx_foreign_id";
        }
    }
    UartRxLog::instance().count(counter);
    UartRxLog::instance().record(packet.data(), packet.size(), true, "[DXL] " + detail);
    if (packet == lastTransmit_)
    {
        lastTransmit_.clear();
        return;
    }
    uint8_t deviceId;
    uint8_t returnLevel;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        deviceId = table_[0x03];
        returnLevel = table_[0x06];
    }
    const uint8_t packetId = packet[2];
    if (packetId != deviceId && packetId != kBroadcastId)
        return;
    if (packet.back() != checksum(packet.data() + 2, packet.size() - 3))
    {
        if (packetId != kBroadcastId)
            sendStatus(kErrorChecksum);
        return;
    }

    const uint8_t instruction = packet[4];
    const std::vector<uint8_t> params(packet.begin() + 5, packet.end() - 1);
    if (instruction == 0x83 || instruction == 0x92)
        return;
    uint8_t error = 0;
    std::vector<uint8_t> response;
    if (instruction == 0x01)
    {
    }
    else if (instruction == 0x02)
    {
        if (params.size() != 2 || static_cast<size_t>(params[0]) + params[1] > 256)
            error = kErrorRange;
        else
        {
            std::lock_guard<std::mutex> lock(mutex_);
            response.assign(table_.begin() + params[0],
                            table_.begin() + params[0] + params[1]);
            if (params[0] <= 0x27 && params[0] + params[1] > 0x27)
                table_[0x27] &= static_cast<uint8_t>(~0x10);
            if (params[0] <= 0x25 && params[0] + params[1] > 0x25)
                table_[0x25] = 0;
        }
    }
    else if (instruction == 0x03 || instruction == 0x04)
    {
        if (params.size() < 2)
            error = kErrorRange;
        else if (instruction == 0x04)
            pendingWrite_ = params;
        else
            applyWrite(params[0], std::vector<uint8_t>(params.begin() + 1,
                                                       params.end()));
    }
    else if (instruction == 0x05)
    {
        if (!pendingWrite_.empty())
        {
            applyWrite(pendingWrite_[0], std::vector<uint8_t>(
                pendingWrite_.begin() + 1, pendingWrite_.end()));
            pendingWrite_.clear();
        }
    }
    else if (instruction == 0x06)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        initializeControlTable(100, 115200);
        saveEepromLocked();
    }
    else if (instruction != 0x08)
        error = kErrorInstruction;

    const bool isRead = instruction == 0x02;
    if (packetId != kBroadcastId &&
        (instruction == 0x01 ||
         (returnLevel != 0 && (returnLevel == 2 || isRead))) &&
        sendStatus(error, response))
        UartTxLog::instance().count("responses_sent");
}

void DxlUartTransport::sendPush()
{
    std::vector<uint8_t> data;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint8_t type = table_[0x39];
        const size_t objectSize = type == 2 ? 30 : type == 1 ? 14 :
                                  type == 5 ? 8 : 12;
        const size_t length = std::min<size_t>(table_.size() - 0x30,
            12 + table_[0x12] * objectSize);
        data.assign(table_.begin() + 0x30, table_.begin() + 0x30 + length);
    }
    const bool sent = sendStatus(0, data);
    if (sent)
        UartTxLog::instance().count("push_sent");
    if (sent && !firstDetectionSent_.exchange(true))
    {
        timespec bootTime{};
        clock_gettime(CLOCK_BOOTTIME, &bootTime);
        const long long milliseconds = bootTime.tv_sec * 1000LL +
                                       bootTime.tv_nsec / 1000000LL;
        std::cout << "BOOT_SLA first_uart_detection_ms=" << milliseconds
                  << " limit_ms=12000 status="
                  << (milliseconds <= 12000 ? "PASS" : "FAIL") << std::endl;
    }
}

void DxlUartTransport::run()
{
    DxlPacketParser parser;
    std::vector<uint8_t> packet;
    std::string pushReason;
    auto nextPush = std::chrono::steady_clock::now();
    while (running_)
    {
        uint8_t chunk[128];
        const ssize_t count = read(serialFd_, chunk, sizeof(chunk));
        const auto now = std::chrono::steady_clock::now();
        if (count > 0)
        {
            UartRxLog::instance().record(chunk, static_cast<size_t>(count), false,
                                         "[DXL] Raw RX chunk");
            parser.feed(chunk, static_cast<size_t>(count), now);
        }
        else if (count < 0 && errno != EAGAIN && errno != EINTR)
        {
            UartRxLog::instance().count("read_errors");
            UartRxLog::instance().event("ERROR", "[DXL] " + errnoText("read"));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        const uint64_t discardedBefore = parser.discarded();
        while (parser.next(packet))
            processPacket(packet);
        if (const size_t dropped = parser.expire(now))
            UartRxLog::instance().event("WARN", "[DXL] dropped " + std::to_string(dropped) +
                " byte(s) of an incomplete packet (no data for 20 ms)");
        if (parser.discarded() != discardedBefore)
            UartRxLog::instance().count("rx_discarded_bytes",
                                        parser.discarded() - discardedBefore);

        bool pushEnabled;
        uint8_t interval;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pushEnabled = table_[0x13] != 0;
            interval = std::max<uint8_t>(1, table_[0x14]);
        }
        // Почему push (не) идёт — в журнал только при смене причины
        const std::string reason =
            !pushEnabled ? "push disabled (control table 0x13 = 0; startup_push or controller WRITE)"
            : !frameReady_ ? "push waiting for the first processed frame"
            : parser.pending() ? "push paused: receiving a controller packet"
            : "push active every " + std::to_string(interval) + " ms";
        if (reason != pushReason && (reason.compare(0, 11, "push paused") != 0))
        {
            UartTxLog::instance().event("INFO", "[DXL] " + reason);
            pushReason = reason;
        }
        if (pushEnabled && frameReady_ && now >= nextPush && !parser.pending())
        {
            sendPush();
            nextPush = now + std::chrono::milliseconds(interval);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
