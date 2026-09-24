#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// Shared, bounded diagnostics. HTTP readers never access the serial port.
// Entry kinds: BYTES (raw chunk), PACKET (parsed frame), INFO (state change),
// WARN, ERROR. Counters are free-form names owned by the active transport.
class UartRxLog
{
public:
    struct Entry {
        uint64_t sequence;
        int64_t timeMs;
        std::string kind, detail, hex;
    };
    struct Snapshot {
        std::string state, device, lastError;
        uint64_t bytes, packets;
        std::map<std::string, uint64_t> counters;
        std::vector<Entry> entries;
    };

    static UartRxLog& instance() { static UartRxLog log; return log; }

    // Changes are also kept as INFO entries, so the history shows when the
    // transport opened, failed or stopped even after the state moved on.
    void state(const std::string& value, const std::string& device)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (value == state_ && device == device_)
            return;
        state_ = value;
        device_ = device;
        pushLocked("INFO", device.empty() ? value : value + " (" + device + ")", "");
    }

    void record(const uint8_t* data, size_t size, bool packet,
                const std::string& detail)
    {
        std::string hex = toHex(data, size);
        std::lock_guard<std::mutex> lock(mutex_);
        if (packet) ++packets_; else bytes_ += size;
        pushLocked(packet ? "PACKET" : "BYTES", detail, std::move(hex));
    }

    // kind: INFO, WARN or ERROR; ERROR also becomes last_error
    void event(const std::string& kind, const std::string& detail)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (kind == "ERROR")
            lastError_ = detail;
        pushLocked(kind, detail, "");
    }

    void count(const std::string& name, uint64_t delta = 1)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_[name] += delta;
    }

    uint64_t counter(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = counters_.find(name);
        return found == counters_.end() ? 0 : found->second;
    }

    // New transport instance: counters and last error describe only it,
    // entries stay as history.
    void resetCounters()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        counters_.clear();
        lastError_.clear();
        bytes_ = packets_ = 0;
    }

    Snapshot snapshot()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {state_, device_, lastError_, bytes_, packets_, counters_,
                {entries_.begin(), entries_.end()}};
    }

    static std::string toHex(const uint8_t* data, size_t size)
    {
        static constexpr char digits[] = "0123456789ABCDEF";
        std::string hex;
        hex.reserve(size * 3);
        for (size_t i = 0; i < size; ++i) {
            if (i) hex += ' ';
            hex += digits[data[i] >> 4];
            hex += digits[data[i] & 15];
        }
        return hex;
    }

protected:
    explicit UartRxLog(const std::string& initialState = "DXL RX disabled or unavailable in this build")
        : state_(initialState) {}

private:
    void pushLocked(const std::string& kind, const std::string& detail, std::string hex)
    {
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        entries_.push_back({++sequence_, now, kind, detail, std::move(hex)});
        if (entries_.size() > 200) entries_.pop_front();
    }

    std::mutex mutex_;
    std::string state_, device_, lastError_;
    uint64_t bytes_ = 0, packets_ = 0, sequence_ = 0;
    std::map<std::string, uint64_t> counters_;
    std::deque<Entry> entries_;
};
