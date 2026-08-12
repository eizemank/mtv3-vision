#pragma once

#include <functional>
#include <memory>

#include <nlohmann/json.hpp>

#include "transport/vision_frame.hpp"

class DxlUartTransport;
class UdpMetadataTransport;

class TransportManager
{
public:
    using DetectorCallback = std::function<bool(uint8_t)>;

    TransportManager(const nlohmann::json& config, DetectorCallback detectorCallback);
    ~TransportManager();

    TransportManager(const TransportManager&) = delete;
    TransportManager& operator=(const TransportManager&) = delete;

    void publish(const VisionFrame& frame);

private:
    std::unique_ptr<DxlUartTransport> uart_;
    std::unique_ptr<UdpMetadataTransport> udp_;
};
