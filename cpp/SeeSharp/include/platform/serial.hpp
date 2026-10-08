#pragma once
// Последовательный порт: termios (POSIX) или Win32 COM-порт (CreateFile/DCB).
// Используется бинарным UART-транспортом, USB-потоком и самотестами.
#include <cstddef>
#include <string>

namespace platform
{
class SerialPort
{
public:
    SerialPort() = default;
    ~SerialPort() { close(); }
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    /// device: /dev/ttyUSB0 или COM3; baud <= 0 — скорость не трогать (USB CDC);
    /// writeOnly — только запись, неблокирующая (запись ждёт готовности до 100 мс);
    /// readTimeoutMs — read() возвращает 0 через столько мс без данных.
    bool open(const std::string& device, int baud, bool writeOnly, int readTimeoutMs,
              std::string& error);
    bool isOpen() const;
    void close();

    /// >0 — байт прочитано, 0 — таймаут, -1 — ошибка (lastError()).
    long read(void* buffer, size_t size);
    /// >0 — байт записано (возможно, не все), 0 — порт не готов (таймаут),
    /// -1 — ошибка (lastError()).
    long write(const void* data, size_t size);
    /// Дождаться ухода буфера передачи (tcdrain / FlushFileBuffers).
    void drain();
    void flushInput();
    const std::string& lastError() const { return error_; }

    static bool baudSupported(int baud);
    static std::string supportedBauds();

private:
#ifdef _WIN32
    void* handle_ = nullptr;           // HANDLE; nullptr == закрыт
#else
    int fd_ = -1;
#endif
    bool writeOnly_ = false;
    std::string error_;
};
}
