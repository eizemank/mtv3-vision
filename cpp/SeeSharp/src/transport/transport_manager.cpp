#include "transport/transport_manager.hpp"

#include <iostream>

#include "transport/binary_uart_transport.hpp"
#include "transport/udp_metadata_transport.hpp"
#include "transport/usb_stream_transport.hpp"
#include "transport/websocket_metadata_transport.hpp"
#ifdef RASPBERRY_CM5
#include "transport/dxl_uart_transport.hpp"
#include "transport/udp_video_transport.hpp"
#endif

TransportManager::TransportManager(const nlohmann::json& config,
                                   DetectorCallback detectorCallback)
{
#ifndef RASPBERRY_CM5
    (void)detectorCallback;
#endif
    const nlohmann::json transports = config.value(
        "transports", nlohmann::json::object());
    const nlohmann::json udp = transports.value(
        "udp_metadata", nlohmann::json::object());
    const std::string udpFormat = udp.value("format", "json");
    if (udp.value("enabled", false) && udpFormat == "json")
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
    else if (udp.value("enabled", false))
    {
        std::cerr << "UDP metadata transport is disabled: unsupported format "
                  << udpFormat << " (supported: json)" << std::endl;
    }

    const nlohmann::json websocket = transports.value(
        "websocket_metadata", nlohmann::json::object());
    if (websocket.value("enabled", false))
    {
        websocket_ = std::make_unique<WebSocketMetadataTransport>(
            websocket.value("bind", "0.0.0.0"),
            static_cast<uint16_t>(websocket.value("port", 5002)));
        if (!websocket_->isOpen())
        {
            std::cerr << "WebSocket metadata transport is disabled: bind failed"
                      << std::endl;
            websocket_.reset();
        }
    }

    const nlohmann::json usb = transports.value(
        "usb_stream", nlohmann::json::object());
    if (usb.value("enabled", false))
    {
        usb_ = std::make_unique<UsbStreamTransport>(
            usb.value("device", "/dev/ttyGS0"),
            usb.value("jpeg_quality", 80), usb.value("max_fps", 15),
            usb.value("max_width", 960), usb.value("metadata", true),
            usb.value("video", true));
    }

#ifdef RASPBERRY_CM5
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
            std::move(detectorCallback),
            uart.value("startup_push", true),
            static_cast<uint8_t>(uart.value("push_interval_ms", 33)));
        if (!uart_->isOpen())
            uart_.reset();
    }
#endif

    const nlohmann::json binaryUart = transports.value(
        "uart_binary", nlohmann::json::object());
    if (binaryUart.value("enabled", false))
    {
        binaryUart_ = std::make_unique<BinaryUartTransport>(
            binaryUart.value("device", "/dev/serial0"),
            binaryUart.value("baud", 115200),
            static_cast<size_t>(binaryUart.value("max_objects", 20)));
        if (!binaryUart_->isOpen())
            binaryUart_.reset();
    }
}

TransportManager::~TransportManager() = default;

void TransportManager::publish(const VisionFrame& frame, const cv::Mat& image)
{
#ifdef RASPBERRY_CM5
    if (uart_)
        uart_->publish(frame);
#endif
    if (binaryUart_)
        binaryUart_->publish(frame);
    if (udp_)
        udp_->publish(frame);
    if (usb_)
        usb_->publish(frame, image);
    if (websocket_)
        websocket_->publish(frame);
#ifdef RASPBERRY_CM5
    if (video_)
        video_->publish(frame.frameId, frame.timestampMs, image);
#endif
}
