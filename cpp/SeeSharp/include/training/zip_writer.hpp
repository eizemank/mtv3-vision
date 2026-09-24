#pragma once
// Минимальный ZIP-писатель (store, без сжатия): JPEG всё равно не жмутся,
// а тянуть zlib/libzip на борт ради экспорта датасета не хочется.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

class ZipWriter
{
public:
    explicit ZipWriter(const std::string& path);
    ~ZipWriter();

    bool ok() const { return static_cast<bool>(out_); }
    bool addFile(const std::string& nameInZip, const std::string& diskPath);
    bool addData(const std::string& nameInZip, const std::string& data);
    /// Пишет центральный каталог; после finish() добавлять нельзя.
    bool finish();

    static uint32_t crc32(const unsigned char* data, size_t size, uint32_t crc = 0);

private:
    struct Entry
    {
        std::string name;
        uint32_t crc = 0;
        uint32_t size = 0;
        uint32_t offset = 0;
        uint16_t dosTime = 0;
        uint16_t dosDate = 0;
    };

    bool writeLocalHeader(Entry& entry);
    void put16(uint16_t value);
    void put32(uint32_t value);

    std::ofstream out_;
    std::vector<Entry> entries_;
    uint64_t position_ = 0;
    bool finished_ = false;
};
