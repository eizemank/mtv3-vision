#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "transport/vision_frame.hpp"

class DxlUartTransport
{
public:
    using DetectorCallback = std::function<bool(uint8_t)>;

    DxlUartTransport(std::string device, int baud, uint8_t deviceId,
                     bool rs485, std::string eepromPath,
                     DetectorCallback detectorCallback,
                     bool startupPush, uint8_t pushIntervalMs);
    ~DxlUartTransport();

    bool isOpen() const { return serialFd_ >= 0; }
    void publish(const VisionFrame& frame);

private:
    void initializeControlTable(uint8_t deviceId, int baud);
    void loadEeprom();
    void saveEepromLocked();
    bool openPort();
    void run();
    void processPacket(const std::vector<uint8_t>& packet);
    bool sendStatus(uint8_t error, const std::vector<uint8_t>& params = {});
    void sendPush();
    bool writeAll(const std::vector<uint8_t>& packet);
    void applyWrite(uint8_t address, const std::vector<uint8_t>& data);

    std::string device_;
    int baud_;
    bool rs485_;
    std::string eepromPath_;
    int serialFd_ = -1;
    std::array<uint8_t, 256> table_{};
    std::vector<uint8_t> pendingWrite_;
    std::vector<uint8_t> lastTransmit_;
    std::mutex mutex_;
    std::atomic<bool> running_{true};
    std::atomic<bool> frameReady_{false};
    std::atomic<bool> firstDetectionSent_{false};
    std::thread worker_;
    DetectorCallback detectorCallback_;
};
