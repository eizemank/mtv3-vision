#include "pipeline/pipeline.hpp"

#include <chrono>

void Pipeline::start()
{
    running_ = true;

    std::thread capture(&Pipeline::captureLoop, this);
    std::thread processing(&Pipeline::processingLoop, this);
    std::thread sink(&Pipeline::sinkLoop, this);

    capture.join();
    processing.join();
    sink.join();
}

void Pipeline::stop()
{
    running_ = false;
    frameQueue_.wake();
    processedQueue_.wake();
}

void Pipeline::captureLoop()
{
    while (running_)
    {
        cv::Mat frame;
        if (!source_.read(frame))
            break;                        // источник иссяк/умер
        frameQueue_.tryPush(std::move(frame));   // полна — дроп кадра
    }
    stop();
}

void Pipeline::processingLoop()
{
    uint32_t frameId = 0;
    while (running_)
    {
        cv::Mat frame;
        if (!frameQueue_.pop(frame, running_))
            continue;                     // разбужены на останов

        ProcessedItem item;
        const auto started = std::chrono::steady_clock::now();
        auto [result, metadata] = manager_.processFrame(frame);
        const auto finished = std::chrono::steady_clock::now();
        item.frameId = ++frameId;
        item.timestampMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                finished.time_since_epoch()).count());
        item.inferenceUs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                finished - started).count());
        item.frame = std::move(frame);
        item.result = std::move(result);
        item.metadata = std::move(metadata);

        processedQueue_.tryPush(std::move(item));
    }
    processedQueue_.wake();
}

void Pipeline::sinkLoop()
{
    while (running_)
    {
        ProcessedItem item;
        if (!processedQueue_.pop(item, running_))
            continue;

        if (!sink_(item))
        {
            stop();
            break;
        }
    }
}
