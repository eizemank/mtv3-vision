#pragma once

#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include "transport/vision_frame.hpp"

class BinaryUartTransport;
class UdpMetadataTransport;
class UsbStreamTransport;
class WebSocketMetadataTransport;
#ifdef RASPBERRY_CM5
class DxlUartTransport;
class UdpVideoTransport;
#endif

class TransportManager
{
public:
    using DetectorCallback = std::function<bool(uint8_t)>;

    TransportManager(const nlohmann::json& config, DetectorCallback detectorCallback);
    ~TransportManager();

    TransportManager(const TransportManager&) = delete;
    TransportManager& operator=(const TransportManager&) = delete;

    void publish(const VisionFrame& frame, const cv::Mat& image);

    // Порт UART, который держит активный транспорт (для самотестов)
    struct UartPort
    {
        std::string device;      // пусто: UART-транспорт не работает
        std::string protocol;    // "dxl" | "binary"
        int baud = 0;
    };
    UartPort uartPort() const { return uartPort_; }

private:
    UartPort uartPort_;
#ifdef RASPBERRY_CM5
    std::unique_ptr<DxlUartTransport> uart_;
#endif
    std::unique_ptr<BinaryUartTransport> binaryUart_;
    std::unique_ptr<UdpMetadataTransport> udp_;
    std::unique_ptr<UsbStreamTransport> usb_;
    std::unique_ptr<WebSocketMetadataTransport> websocket_;
#ifdef RASPBERRY_CM5
    std::unique_ptr<UdpVideoTransport> video_;
#endif
};
