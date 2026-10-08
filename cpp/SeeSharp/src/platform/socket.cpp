#include "platform/socket.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <mutex>
#include <system_error>

#ifndef _WIN32
#include <fcntl.h>
#include <poll.h>
#include <sys/time.h>
#endif

namespace platform
{
#ifdef _WIN32

bool socketsInit()
{
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, [] {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    });
    return ok;
}

void socketClose(socket_t s)
{
    if (socketValid(s))
        closesocket(s);
}

int socketError() { return WSAGetLastError(); }

std::string socketErrorText(const char* call)
{
    const int error = socketError();
    return std::string(call) + ": " + std::system_category().message(error) +
           " (" + std::to_string(error) + ")";
}

bool socketWouldBlock(int error) { return error == WSAEWOULDBLOCK; }
bool socketInterrupted(int error) { return error == WSAEINTR; }

long socketRecv(socket_t s, void* buffer, size_t size)
{
    const int result = recv(s, static_cast<char*>(buffer),
                            static_cast<int>(std::min<size_t>(size, INT_MAX)), 0);
    return result == SOCKET_ERROR ? -1 : result;
}

long socketSend(socket_t s, const void* data, size_t size)
{
    const int result = send(s, static_cast<const char*>(data),
                            static_cast<int>(std::min<size_t>(size, INT_MAX)), 0);
    return result == SOCKET_ERROR ? -1 : result;
}

long socketSendTo(socket_t s, const void* data, size_t size, const sockaddr_in& to)
{
    const int result = sendto(s, static_cast<const char*>(data),
                              static_cast<int>(std::min<size_t>(size, INT_MAX)), 0,
                              reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    return result == SOCKET_ERROR ? -1 : result;
}

namespace
{
bool setTimeout(socket_t s, int option, int milliseconds)
{
    const DWORD value = static_cast<DWORD>(std::max(0, milliseconds));
    return setsockopt(s, SOL_SOCKET, option, reinterpret_cast<const char*>(&value),
                      sizeof(value)) == 0;
}
}

bool socketSetReceiveTimeout(socket_t s, int ms) { return setTimeout(s, SO_RCVTIMEO, ms); }
bool socketSetSendTimeout(socket_t s, int ms) { return setTimeout(s, SO_SNDTIMEO, ms); }

bool socketSetNonBlocking(socket_t s)
{
    u_long mode = 1;
    return ioctlsocket(s, FIONBIO, &mode) == 0;
}

// SO_REUSEADDR на Windows позволяет чужому процессу перехватить порт;
// повторный bind после закрытия и так разрешён, поэтому ничего не делаем.
bool socketSetReuseAddress(socket_t) { return true; }

int socketPoll(socket_t s, bool forWrite, int timeoutMs)
{
    WSAPOLLFD descriptor{};
    descriptor.fd = s;
    descriptor.events = forWrite ? POLLWRNORM : POLLRDNORM;
    const int result = WSAPoll(&descriptor, 1, timeoutMs);
    return result == SOCKET_ERROR ? -1 : result;
}

#else  // POSIX

bool socketsInit() { return true; }

void socketClose(socket_t s)
{
    if (socketValid(s))
        close(s);
}

int socketError() { return errno; }

std::string socketErrorText(const char* call)
{
    return std::string(call) + ": " + std::strerror(errno);
}

bool socketWouldBlock(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
bool socketInterrupted(int error) { return error == EINTR; }

long socketRecv(socket_t s, void* buffer, size_t size)
{
    return static_cast<long>(recv(s, buffer, size, 0));
}

long socketSend(socket_t s, const void* data, size_t size)
{
    return static_cast<long>(send(s, data, size, MSG_NOSIGNAL));
}

long socketSendTo(socket_t s, const void* data, size_t size, const sockaddr_in& to)
{
    return static_cast<long>(sendto(s, data, size, MSG_DONTWAIT | MSG_NOSIGNAL,
                                    reinterpret_cast<const sockaddr*>(&to), sizeof(to)));
}

namespace
{
bool setTimeout(socket_t s, int option, int milliseconds)
{
    timeval value{};
    value.tv_sec = std::max(0, milliseconds) / 1000;
    value.tv_usec = (std::max(0, milliseconds) % 1000) * 1000;
    return setsockopt(s, SOL_SOCKET, option, &value, sizeof(value)) == 0;
}
}

bool socketSetReceiveTimeout(socket_t s, int ms) { return setTimeout(s, SO_RCVTIMEO, ms); }
bool socketSetSendTimeout(socket_t s, int ms) { return setTimeout(s, SO_SNDTIMEO, ms); }

bool socketSetNonBlocking(socket_t s)
{
    const int flags = fcntl(s, F_GETFL);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool socketSetReuseAddress(socket_t s)
{
    int one = 1;
    return setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == 0;
}

int socketPoll(socket_t s, bool forWrite, int timeoutMs)
{
    pollfd descriptor{s, static_cast<short>(forWrite ? POLLOUT : POLLIN), 0};
    return poll(&descriptor, 1, timeoutMs);
}

#endif
}
