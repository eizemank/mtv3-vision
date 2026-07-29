#pragma once
// ShmSource — источник кадров с MTV3: читает NV12-ринг, который пишет
// mtv3_cam_daemon --shm в /dev/shm/mtv3cam, и отдаёт BGR cv::Mat.
// API повторяет cv::VideoCapture в объёме, нужном main.cpp
// (read/isOpened/release), поэтому captureThread шаблонизируется без правок.
//
// Протокол v1 (little-endian, заголовок 64 Б + слоты подряд):
//   u32 magic("M3SH"=0x4D335348), version, w, h, stride, fmt(0=NV12),
//   nslot, slot_size, seq, ts_ms, reserved[6]
// seq — число завершённых кадров; последний в слоте (seq-1) % nslot.
// Писатель инкрементирует seq после memcpy (с барьером); читатель перечитывает
// seq после копии — прирост >= nslot-1 означает порванный кадр (ретрай).

#include <cstdint>
#include <opencv2/core.hpp>

#include "pipeline/i_frame_source.hpp"

class ShmSource : public IFrameSource
{
public:
    // timeout_ms — и на ожидание появления ринга (старт демона), и на кадр
    explicit ShmSource(const char* path = "/dev/shm/mtv3cam",
                       int timeout_ms = 5000);
    ~ShmSource();

    ShmSource(const ShmSource&) = delete;
    ShmSource& operator=(const ShmSource&) = delete;

    bool isOpened() const override { return hdr_ != nullptr; }
    bool read(cv::Mat& bgr) override;   // блокируется до нового кадра; false = таймаут
    void release() override;

    int width() const;
    int height() const;

private:
    struct Header
    {
        uint32_t magic, version;
        uint32_t w, h, stride, fmt;
        uint32_t nslot, slot_size;
        volatile uint32_t seq;
        uint32_t ts_ms;
        uint32_t reserved[6];
    };
    static_assert(sizeof(Header) == 64, "shm header must be 64 bytes");

    const Header* hdr_ = nullptr;
    const uint8_t* data_ = nullptr;   // слоты (сразу за заголовком)
    size_t maplen_ = 0;
    uint32_t lastSeq_ = 0;
    int timeoutMs_;

    bool openWait(const char* path);
};
