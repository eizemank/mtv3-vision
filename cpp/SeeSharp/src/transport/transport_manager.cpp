#include "transport/transport_manager.hpp"

#include <iostream>

#include "transport/dxl_uart_transport.hpp"
#include "transport/udp_metadata_transport.hpp"
#include "transport/udp_video_transport.hpp"

TransportManager::TransportManager(const nlohmann::json& config,
                                   DetectorCallback detectorCallback)
{
    const nlohmann::json transports = config.value(
        "transports", nlohmann::json::object());
    const nlohmann::json udp = transports.value(
        "udp_metadata", nlohmann::json::object());
    if (udp.value("enabled", false))
    {
        udp_ = std::make_unique<UdpMetadataTransport>(
            udp.value("host", "127.0.0.1"),
            static_cast<uint16_t>(udp.value("port", 5000)));
        if (!udp_->isOpen())
        {
            std::cerr << "UDP metadata transport is disabled: invalid endpoint"
                      << std::endl;
            udp_.reset();
        }
    }

    const nlohmann::json video = transports.value(
        "udp_video", nlohmann::json::object());
    if (video.value("enabled", false))
    {
        video_ = std::make_unique<UdpVideoTransport>(
            video.value("host", "127.0.0.1"),
            static_cast<uint16_t>(video.value("port", 5001)),
            video.value("jpeg_quality", 80),
            static_cast<size_t>(video.value("packet_size", 1400)),
            video.value("max_fps", 15));
        if (!video_->isOpen())
        {
            std::cerr << "UDP video transport is disabled: invalid endpoint"
                      << std::endl;
            video_.reset();
        }
    }

    const nlohmann::json uart = transports.value(
        "uart_dxl", nlohmann::json::object());
    if (uart.value("enabled", false))
    {
        uart_ = std::make_unique<DxlUartTransport>(
            uart.value("device", "/dev/serial0"),
            uart.value("baud", 115200),
            static_cast<uint8_t>(uart.value("id", 100)),
            uart.value("rs485", false),
            uart.value("eeprom_file", "dxl_eeprom.bin"),
            std::move(detectorCallback));
        if (!uart_->isOpen())
            uart_.reset();
    }
}

TransportManager::~TransportManager() = default;

void TransportManager::publish(const VisionFrame& frame, const cv::Mat& image)
{
    if (uart_)
        uart_->publish(frame);
    if (udp_)
        udp_->publish(frame);
    if (video_)
        video_->publish(frame.frameId, frame.timestampMs, image);
}
