#pragma once
// Pipeline — трёхпоточный конвейер capture -> process -> sink
// (порт Pipeline из SeeSharpPy: bounded-очереди с backpressure — при
// медленной обработке кадры дропаются, память не растёт).
//
// SRP: только оркестрация потоков. DIP: зависит от IFrameSource и
// ProcessingManager; вывод инжектируется как sink-колбэк (imshow на десктопе,
// headless/отправка метаданных на модуле).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include "model/blob_meta_data.hpp"
#include "pipeline/i_frame_source.hpp"
#include "processing/processing_manager.hpp"

/// Результат обработки одного кадра, приходит в sink
struct ProcessedItem
{
    uint32_t frameId = 0;
    uint32_t timestampMs = 0;
    uint32_t inferenceUs = 0;
    cv::Mat frame;                       // исходный кадр
    cv::Mat result;                      // аннотированный кадр
    std::vector<BlobMetaData> metadata;  // найденные объекты
};

class Pipeline
{
public:
    /// sink возвращает false — остановить конвейер (ESC на десктопе)
    using Sink = std::function<bool(const ProcessedItem&)>;

    Pipeline(IFrameSource& source, ProcessingManager& manager, Sink sink)
        : source_(source), manager_(manager), sink_(std::move(sink)) {}

    /// Запуск; блокируется до остановки (конец источника, stop(), sink=false)
    void start();
    void stop();

private:
    static constexpr size_t kQueueMax = 2;   // как QUEUE_MAX_SIZE в python

    template <typename T>
    class BoundedQueue
    {
    public:
        /// false = очередь полна, элемент дропнут (backpressure)
        bool tryPush(T&& v)
        {
            {
                std::lock_guard<std::mutex> lock(m_);
                if (q_.size() >= kQueueMax)
                    return false;
                q_.push(std::move(v));
            }
            cv_.notify_one();
            return true;
        }

        /// false = разбужены на останов при пустой очереди
        bool pop(T& out, const std::atomic<bool>& running)
        {
            std::unique_lock<std::mutex> lock(m_);
            cv_.wait(lock, [&] { return !q_.empty() || !running; });
            if (q_.empty())
                return false;
            out = std::move(q_.front());
            q_.pop();
            return true;
        }

        void wake() { cv_.notify_all(); }

    private:
        std::queue<T> q_;
        std::mutex m_;
        std::condition_variable cv_;
    };

    void captureLoop();
    void processingLoop();
    void sinkLoop();

    IFrameSource& source_;
    ProcessingManager& manager_;
    Sink sink_;
    std::atomic<bool> running_{false};
    BoundedQueue<cv::Mat> frameQueue_;
    BoundedQueue<ProcessedItem> processedQueue_;
};
