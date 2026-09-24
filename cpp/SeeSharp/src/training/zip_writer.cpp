#include "training/zip_writer.hpp"

#include <ctime>
#include <sys/stat.h>

namespace
{
uint32_t gCrcTable[256];
bool gCrcReady = false;

void initCrc()
{
    if (gCrcReady)
        return;
    for (uint32_t i = 0; i < 256; ++i)
    {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        gCrcTable[i] = c;
    }
    gCrcReady = true;
}

void dosDateTime(time_t when, uint16_t& date, uint16_t& time)
{
    struct tm t{};
    localtime_r(&when, &t);
    const int year = t.tm_year + 1900 < 1980 ? 1980 : t.tm_year + 1900;
    date = static_cast<uint16_t>(((year - 1980) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    time = static_cast<uint16_t>((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
}
}  // namespace

ZipWriter::ZipWriter(const std::string& path) : out_(path, std::ios::binary | std::ios::trunc)
{
    initCrc();
}

ZipWriter::~ZipWriter()
{
    if (!finished_ && out_)
        finish();
}

uint32_t ZipWriter::crc32(const unsigned char* data, size_t size, uint32_t crc)
{
    initCrc();
    crc = ~crc;
    for (size_t i = 0; i < size; ++i)
        crc = gCrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void ZipWriter::put16(uint16_t value)
{
    const unsigned char bytes[2] = {static_cast<unsigned char>(value & 0xFF),
                                    static_cast<unsigned char>(value >> 8)};
    out_.write(reinterpret_cast<const char*>(bytes), 2);
    position_ += 2;
}

void ZipWriter::put32(uint32_t value)
{
    const unsigned char bytes[4] = {
        static_cast<unsigned char>(value & 0xFF), static_cast<unsigned char>((value >> 8) & 0xFF),
        static_cast<unsigned char>((value >> 16) & 0xFF), static_cast<unsigned char>(value >> 24)};
    out_.write(reinterpret_cast<const char*>(bytes), 4);
    position_ += 4;
}

bool ZipWriter::writeLocalHeader(Entry& entry)
{
    entry.offset = static_cast<uint32_t>(position_);
    put32(0x04034b50);           // local file header
    put16(20);                   // version needed
    put16(0x0800);               // flags: UTF-8 names
    put16(0);                    // method: store
    put16(entry.dosTime);
    put16(entry.dosDate);
    put32(entry.crc);
    put32(entry.size);
    put32(entry.size);
    put16(static_cast<uint16_t>(entry.name.size()));
    put16(0);                    // extra length
    out_.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
    position_ += entry.name.size();
    return static_cast<bool>(out_);
}

bool ZipWriter::addData(const std::string& nameInZip, const std::string& data)
{
    if (!out_ || finished_ || nameInZip.empty() || nameInZip.size() > 65535)
        return false;
    Entry entry;
    entry.name = nameInZip;
    entry.size = static_cast<uint32_t>(data.size());
    entry.crc = crc32(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    dosDateTime(time(nullptr), entry.dosDate, entry.dosTime);
    if (!writeLocalHeader(entry))
        return false;
    out_.write(data.data(), static_cast<std::streamsize>(data.size()));
    position_ += data.size();
    entries_.push_back(entry);
    return static_cast<bool>(out_);
}

bool ZipWriter::addFile(const std::string& nameInZip, const std::string& diskPath)
{
    if (!out_ || finished_ || nameInZip.empty() || nameInZip.size() > 65535)
        return false;
    std::ifstream in(diskPath, std::ios::binary);
    if (!in)
        return false;
    struct stat st{};
    stat(diskPath.c_str(), &st);
    if (static_cast<uint64_t>(st.st_size) > 0xFFFFFFFFull)
        return false;

    Entry entry;
    entry.name = nameInZip;
    entry.size = static_cast<uint32_t>(st.st_size);
    dosDateTime(st.st_mtime, entry.dosDate, entry.dosTime);
    // CRC считаем заранее (файлы маленькие), чтобы не использовать data descriptor
    uint32_t crc = 0;
    std::vector<unsigned char> buffer(64 * 1024);
    while (in)
    {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0)
            crc = crc32(buffer.data(), static_cast<size_t>(got), crc);
    }
    entry.crc = crc;
    if (!writeLocalHeader(entry))
        return false;
    in.clear();
    in.seekg(0);
    uint64_t written = 0;
    while (in)
    {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = in.gcount();
        if (got > 0)
        {
            out_.write(reinterpret_cast<const char*>(buffer.data()), got);
            written += static_cast<uint64_t>(got);
        }
    }
    position_ += written;
    if (written != entry.size)
        return false;
    entries_.push_back(entry);
    return static_cast<bool>(out_);
}

bool ZipWriter::finish()
{
    if (!out_ || finished_)
        return false;
    finished_ = true;
    const uint64_t centralStart = position_;
    for (const Entry& entry : entries_)
    {
        put32(0x02014b50);       // central directory header
        put16(20);               // version made by
        put16(20);               // version needed
        put16(0x0800);
        put16(0);
        put16(entry.dosTime);
        put16(entry.dosDate);
        put32(entry.crc);
        put32(entry.size);
        put32(entry.size);
        put16(static_cast<uint16_t>(entry.name.size()));
        put16(0);                // extra
        put16(0);                // comment
        put16(0);                // disk
        put16(0);                // internal attrs
        put32(0);                // external attrs
        put32(entry.offset);
        out_.write(entry.name.data(), static_cast<std::streamsize>(entry.name.size()));
        position_ += entry.name.size();
    }
    const uint64_t centralSize = position_ - centralStart;
    put32(0x06054b50);           // end of central directory
    put16(0);
    put16(0);
    put16(static_cast<uint16_t>(entries_.size()));
    put16(static_cast<uint16_t>(entries_.size()));
    put32(static_cast<uint32_t>(centralSize));
    put32(static_cast<uint32_t>(centralStart));
    put16(0);
    out_.flush();
    const bool ok = static_cast<bool>(out_);
    out_.close();
    return ok;
}
