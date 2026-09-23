#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

// Shared, bounded diagnostics. HTTP readers never access the serial port.
class UartRxLog
{
public:
    struct Entry {
        uint64_t sequence;
        int64_t timeMs;
        std::string kind, detail, hex;
    };
    struct Snapshot {
        std::string state, device;
        uint64_t bytes, packets;
        std::vector<Entry> entries;
    };

    static UartRxLog& instance() { static UartRxLog log; return log; }

    void state(const std::string& value, const std::string& device)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = value;
        device_ = device;
    }

    void record(const uint8_t* data, size_t size, bool packet,
                const std::string& detail)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string hex;
        for (size_t i = 0; i < size; ++i) {
            if (i) hex += ' ';
            hex += digits[data[i] >> 4];
            hex += digits[data[i] & 15];
        }
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        std::lock_guard<std::mutex> lock(mutex_);
        if (packet) ++packets_; else bytes_ += size;
        entries_.push_back({++sequence_, now, packet ? "PACKET" : "BYTES", detail, hex});
        if (entries_.size() > 100) entries_.pop_front();
    }

    Snapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {state_, device_, bytes_, packets_, {entries_.begin(), entries_.end()}};
    }

protected:
    explicit UartRxLog(const std::string& initialState = "DXL RX disabled or unavailable in this build")
        : state_(initialState) {}

private:
    std::mutex mutex_;
    std::string state_, device_;
    uint64_t bytes_ = 0, packets_ = 0, sequence_ = 0;
    std::deque<Entry> entries_;
};
