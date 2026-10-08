#include "platform/serial.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <system_error>

#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace platform
{
#ifdef _WIN32

namespace
{
std::string win32ErrorText(const char* call)
{
    const DWORD error = GetLastError();
    return std::string(call) + ": " + std::system_category().message(static_cast<int>(error)) +
           " (" + std::to_string(error) + ")";
}
}

bool SerialPort::baudSupported(int baud) { return baud > 0; }
std::string SerialPort::supportedBauds() { return "any positive value"; }

bool SerialPort::open(const std::string& device, int baud, bool writeOnly,
                      int readTimeoutMs, std::string& error)
{
    close();
    writeOnly_ = writeOnly;
    // COM10 и выше открываются только через \\.\COM10; для COM1..9 префикс тоже
    // допустим
    const std::string path = device.rfind("\\\\.\\", 0) == 0 ? device : "\\\\.\\" + device;
    HANDLE handle = CreateFileA(path.c_str(), writeOnly ? GENERIC_WRITE
                                                          : (GENERIC_READ | GENERIC_WRITE),
                                0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        error = error_ = win32ErrorText(("open " + device).c_str());
        return false;
    }
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(handle, &dcb))
    {
        error = error_ = win32ErrorText("GetCommState");
        CloseHandle(handle);
        return false;
    }
    if (baud > 0)
        dcb.BaudRate = static_cast<DWORD>(baud);
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;   // USB-UART адаптеры ждут DTR
    if (!SetCommState(handle, &dcb))
    {
        error = error_ = win32ErrorText("SetCommState");
        CloseHandle(handle);
        return false;
    }
    // ReadInterval=MAXDWORD + Multiplier=MAXDWORD + Constant: вернуть то, что
    // уже есть в буфере, иначе ждать до Constant мс (аналог VMIN=0/VTIME)
    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = static_cast<DWORD>(std::max(1, readTimeoutMs));
    timeouts.WriteTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    if (!SetCommTimeouts(handle, &timeouts))
    {
        error = error_ = win32ErrorText("SetCommTimeouts");
        CloseHandle(handle);
        return false;
    }
    PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    handle_ = handle;
    error_.clear();
    return true;
}

bool SerialPort::isOpen() const { return handle_ != nullptr; }

void SerialPort::close()
{
    if (handle_)
        CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
}

long SerialPort::read(void* buffer, size_t size)
{
    if (!handle_)
        return -1;
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), buffer,
                  static_cast<DWORD>(std::min<size_t>(size, MAXDWORD)), &got, nullptr))
    {
        error_ = win32ErrorText("ReadFile");
        return -1;
    }
    return static_cast<long>(got);
}

long SerialPort::write(const void* data, size_t size)
{
    if (!handle_)
        return -1;
    DWORD written = 0;
    if (!WriteFile(static_cast<HANDLE>(handle_), data,
                   static_cast<DWORD>(std::min<size_t>(size, MAXDWORD)), &written, nullptr))
    {
        error_ = win32ErrorText("WriteFile");
        return -1;
    }
    return static_cast<long>(written);   // 0 == WriteTotalTimeout истёк
}

void SerialPort::drain()
{
    if (handle_)
        FlushFileBuffers(static_cast<HANDLE>(handle_));
}

void SerialPort::flushInput()
{
    if (handle_)
        PurgeComm(static_cast<HANDLE>(handle_), PURGE_RXCLEAR);
}

#else  // POSIX

namespace
{
speed_t baudConstant(int baud)
{
    switch (baud)
    {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
#ifdef B230400
        case 230400: return B230400;
#endif
#ifdef B460800
        case 460800: return B460800;
#endif
#ifdef B500000
        case 500000: return B500000;
#endif
#ifdef B921600
        case 921600: return B921600;
#endif
#ifdef B1000000
        case 1000000: return B1000000;
#endif
        default: return 0;
    }
}

std::string errnoText(const char* call)
{
    return std::string(call) + ": " + std::strerror(errno);
}
}

bool SerialPort::baudSupported(int baud) { return baudConstant(baud) != 0; }
std::string SerialPort::supportedBauds()
{
    return "9600..115200, 230400, 460800, 500000, 921600, 1000000";
}

bool SerialPort::open(const std::string& device, int baud, bool writeOnly,
                      int readTimeoutMs, std::string& error)
{
    close();
    writeOnly_ = writeOnly;
    const speed_t speed = baud > 0 ? baudConstant(baud) : 0;
    if (baud > 0 && !speed)
    {
        error = error_ = "unsupported baud " + std::to_string(baud) +
                         " (supported: " + supportedBauds() + ")";
        return false;
    }
    // O_RDWR: RX монитор видит эхо/петлю; writeOnly — неблокирующая запись
    // (USB-гаджет: poll на готовность вместо зависания в write)
    fd_ = ::open(device.c_str(), writeOnly ? (O_WRONLY | O_NOCTTY | O_NONBLOCK)
                                           : (O_RDWR | O_NOCTTY));
    if (fd_ < 0)
    {
        error = error_ = errnoText(("open " + device).c_str());
        return false;
    }
    termios options{};
    if (tcgetattr(fd_, &options) != 0)
    {
        error = error_ = errnoText("tcgetattr");
        if (writeOnly)
        {
            // USB CDC gadget без tty-атрибутов: писать всё равно можно
            error.clear();
            error_.clear();
            return true;
        }
        close();
        return false;
    }
    cfmakeraw(&options);
    if (speed)
    {
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);
        // cfmakeraw не сбрасывает унаследованные стоп-биты/аппаратный flow control
        options.c_cflag &= ~(CSTOPB | CRTSCTS);
        options.c_cflag |= CLOCAL | CREAD;
    }
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = static_cast<cc_t>(std::clamp(readTimeoutMs / 100, 1, 255));
    if (tcsetattr(fd_, TCSANOW, &options) != 0)
    {
        error = error_ = errnoText("tcsetattr");
        if (writeOnly)
        {
            error.clear();
            error_.clear();
            return true;
        }
        close();
        return false;
    }
    tcflush(fd_, TCIFLUSH);
    error_.clear();
    return true;
}

bool SerialPort::isOpen() const { return fd_ >= 0; }

void SerialPort::close()
{
    if (fd_ >= 0)
        ::close(fd_);
    fd_ = -1;
}

long SerialPort::read(void* buffer, size_t size)
{
    if (fd_ < 0)
        return -1;
    for (;;)
    {
        const ssize_t count = ::read(fd_, buffer, size);
        if (count >= 0)
            return static_cast<long>(count);
        if (errno == EINTR || errno == EAGAIN)
            return 0;
        error_ = errnoText("read");
        return -1;
    }
}

long SerialPort::write(const void* data, size_t size)
{
    if (fd_ < 0)
        return -1;
    for (;;)
    {
        const ssize_t written = ::write(fd_, data, size);
        if (written >= 0)
            return static_cast<long>(written);
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN)
        {
            pollfd descriptor{fd_, POLLOUT, 0};
            if (poll(&descriptor, 1, 100) > 0)
                continue;
            return 0;
        }
        error_ = errnoText("write");
        return -1;
    }
}

void SerialPort::drain()
{
    if (fd_ >= 0)
        tcdrain(fd_);
}

void SerialPort::flushInput()
{
    if (fd_ >= 0)
        tcflush(fd_, TCIFLUSH);
}

#endif
}
