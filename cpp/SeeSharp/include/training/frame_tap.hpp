#pragma once
// FrameTap — точка съёма кадров из sink конвейера для сбора датасета и
// GET /metadata/last. Кадр копируется только пока кто-то «взвёл» тап
// (arm()), иначе в sink остаётся один atomic load. Метаданные (маленькие)
// сохраняются на каждом кадре — они нужны для «текущего предсказания» в UI.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

#include <opencv2/core.hpp>

#include "model/blob_meta_data.hpp"
#include "model/common_config.hpp"

struct FrameSnapshot
{
    uint64_t sequence = 0;          // номер съёма (растёт на каждом кадре)
    uint32_t frameId = 0;
    uint32_t timestampMs = 0;
    ProcessingType type = ProcessingType::Off;
    cv::Mat frame;                  // копия исходного кадра (только при arm)
    std::vector<BlobMetaData> metadata;
};

struct MetadataSnapshot
{
    uint64_t sequence = 0;
    uint32_t frameId = 0;
    uint32_t timestampMs = 0;
    int64_t wallTimeMs = 0;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    ProcessingType type = ProcessingType::Off;
    std::vector<BlobMetaData> metadata;
};

class FrameTap
{
public:
    void arm() { ++armed_; }
    void disarm() { if (armed_ > 0) --armed_; }

    /// Вызывается из sink на каждом кадре.
    void offer(const cv::Mat& frame, const std::vector<BlobMetaData>& metadata,
               uint32_t frameId, uint32_t timestampMs, ProcessingType type, double fps)
    {
        const auto wall = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(mutex_);
        ++sequence_;
        last_.sequence = sequence_;
        last_.frameId = frameId;
        last_.timestampMs = timestampMs;
        last_.wallTimeMs = wall;
        last_.width = frame.cols;
        last_.height = frame.rows;
        last_.fps = fps;
        last_.type = type;
        last_.metadata = metadata;
        if (armed_.load(std::memory_order_relaxed) > 0)
        {
            snapshot_.sequence = sequence_;
            snapshot_.frameId = frameId;
            snapshot_.timestampMs = timestampMs;
            snapshot_.type = type;
            snapshot_.frame = frame.clone();
            snapshot_.metadata = metadata;
            ready_.notify_all();
        }
    }

    /// Ждёт кадр с sequence > afterSequence (тап должен быть взведён).
    bool waitNext(uint64_t afterSequence, std::chrono::milliseconds timeout, FrameSnapshot& out)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!ready_.wait_for(lock, timeout, [&] {
                return snapshot_.sequence > afterSequence && !snapshot_.frame.empty(); }))
            return false;
        out = snapshot_;
        return true;
    }

    MetadataSnapshot lastMetadata() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::atomic<int> armed_{0};
    uint64_t sequence_ = 0;
    FrameSnapshot snapshot_;
    MetadataSnapshot last_;
};
