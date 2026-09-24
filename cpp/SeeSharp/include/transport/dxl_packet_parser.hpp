#pragma once

// Dynamixel 1.0 packet framing (FF FF ID LEN INSTR PARAMS... CHK), no I/O.
// Shared by DxlUartTransport, the developer self-tests and
// tests/uart_protocol.test.cpp.
//
// A partial packet older than `timeout` is dropped: a few bytes of line noise
// must not stall the parser (and the push that waits for an idle parser).

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

class DxlPacketParser
{
public:
    using Clock = std::chrono::steady_clock;

    explicit DxlPacketParser(std::chrono::milliseconds timeout = std::chrono::milliseconds(20))
        : timeout_(timeout) {}

    void feed(const uint8_t* data, size_t size, Clock::time_point now)
    {
        if (!size)
            return;
        buffer_.insert(buffer_.end(), data, data + size);
        lastInput_ = now;
    }

    // Extracts the next complete packet (checksum is not verified here:
    // the transport answers a bad checksum with a status error).
    bool next(std::vector<uint8_t>& packet)
    {
        while (buffer_.size() >= 2)
        {
            size_t start = 0;
            while (start + 1 < buffer_.size() &&
                   !(buffer_[start] == 0xFF && buffer_[start + 1] == 0xFF))
                ++start;
            if (start + 1 >= buffer_.size())
            {
                // keep a trailing FF: it may be the first half of a header
                discard(buffer_.back() == 0xFF ? buffer_.size() - 1 : buffer_.size());
                return false;
            }
            discard(start);
            if (buffer_.size() < 4)
                return false;
            // ID FF is invalid in DXL 1.0: "FF FF FF" means the header starts
            // one byte later; LEN < 2 cannot hold INSTR + CHK.
            if (buffer_[2] == 0xFF || buffer_[3] < 2)
            {
                discard(1);
                continue;
            }
            const size_t total = 4 + static_cast<size_t>(buffer_[3]);
            if (buffer_.size() < total)
                return false;
            packet.assign(buffer_.begin(), buffer_.begin() + total);
            buffer_.erase(buffer_.begin(), buffer_.begin() + total);
            return true;
        }
        return false;
    }

    // Returns the number of bytes dropped because the rest of the packet
    // never arrived.
    size_t expire(Clock::time_point now)
    {
        if (buffer_.empty() || now - lastInput_ < timeout_)
            return 0;
        const size_t dropped = buffer_.size();
        discard(dropped);
        return dropped;
    }

    size_t pending() const { return buffer_.size(); }
    uint64_t discarded() const { return discarded_; }

private:
    void discard(size_t count)
    {
        buffer_.erase(buffer_.begin(), buffer_.begin() + count);
        discarded_ += count;
    }

    std::chrono::milliseconds timeout_;
    std::vector<uint8_t> buffer_;
    Clock::time_point lastInput_{};
    uint64_t discarded_ = 0;
};
