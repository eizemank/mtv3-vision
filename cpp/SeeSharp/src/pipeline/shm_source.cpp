#include "pipeline/shm_source.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <thread>

#include <opencv2/imgproc.hpp>

namespace
{
constexpr uint32_t kMagic = 0x4D335348u;   // "M3SH"

int64_t nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

ShmSource::ShmSource(const char* path, int timeout_ms)
    : timeoutMs_(timeout_ms)
{
    openWait(path);
}

ShmSource::~ShmSource()
{
    release();
}

bool ShmSource::openWait(const char* path)
{
    const int64_t deadline = nowMs() + timeoutMs_;
    while (nowMs() < deadline)
    {
        int fd = ::open(path, O_RDONLY);
        if (fd < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        struct stat st{};
        if (fstat(fd, &st) < 0 || st.st_size <= (off_t)sizeof(Header))
        {
            ::close(fd);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        void* map = mmap(nullptr, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        ::close(fd);
        if (map == MAP_FAILED)
            return false;

        const auto* hdr = static_cast<const Header*>(map);
        if (hdr->magic != kMagic || hdr->fmt != 0)
        {
            munmap(map, st.st_size);   // демон ещё пишет заголовок — подождать
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        hdr_ = hdr;
        data_ = reinterpret_cast<const uint8_t*>(hdr + 1);
        maplen_ = st.st_size;
        lastSeq_ = hdr->seq;
        return true;
    }
    return false;
}

int ShmSource::width() const  { return hdr_ ? (int)hdr_->w : 0; }
int ShmSource::height() const { return hdr_ ? (int)hdr_->h : 0; }

bool ShmSource::read(cv::Mat& bgr)
{
    if (!hdr_)
        return false;

    // ждать нового кадра
    const int64_t deadline = nowMs() + timeoutMs_;
    for (;;)
    {
        const uint32_t seq = hdr_->seq;
        if (seq != 0 && seq != lastSeq_)
            break;
        if (nowMs() > deadline)
            return false;              // демон умер/завис — источник "иссяк"
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const int w = hdr_->w, h = hdr_->h, stride = hdr_->stride;
    for (int attempt = 0; attempt < 3; ++attempt)   // анти-tearing
    {
        const uint32_t seq = hdr_->seq;
        const uint8_t* slot = data_ + (size_t)((seq - 1) % hdr_->nslot) * hdr_->slot_size;

        cv::Mat nv12(h * 3 / 2, w, CV_8UC1);
        if (stride == w)
        {
            std::memcpy(nv12.data, slot, (size_t)w * h * 3 / 2);
        }
        else
        {
            for (int r = 0; r < h * 3 / 2; ++r)
                std::memcpy(nv12.ptr(r), slot + (size_t)r * stride, w);
        }

        if (hdr_->seq - seq < hdr_->nslot - 1)      // слот не перезаписали
        {
            lastSeq_ = seq;
            cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);
            return true;
        }
    }
    return false;
}

void ShmSource::release()
{
    if (hdr_)
    {
        munmap(const_cast<Header*>(hdr_), maplen_);
        hdr_ = nullptr;
        data_ = nullptr;
    }
}
