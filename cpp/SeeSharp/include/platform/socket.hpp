#pragma once
// Кроссплатформенные сокеты: POSIX (Linux, CM5, MTV3) и WinSock2 (Windows,
// MinGW-w64). Код транспортов и HTTP-сервера пишется один раз через эти
// обёртки; различия (SOCKET vs int, closesocket, таймауты в DWORD мс,
// отсутствие MSG_NOSIGNAL/EINTR) спрятаны здесь.
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include "platform/windows.hpp"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace platform
{
#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

inline bool socketValid(socket_t s) { return s != kInvalidSocket; }

/// Инициализация подсистемы (WSAStartup один раз); на POSIX всегда true.
bool socketsInit();
void socketClose(socket_t s);

/// Код последней ошибки (errno / WSAGetLastError) и его текст.
int socketError();
std::string socketErrorText(const char* call);
bool socketWouldBlock(int error);
bool socketInterrupted(int error);

/// recv/send: >0 байт, 0 — соединение закрыто, -1 — ошибка (socketError()).
long socketRecv(socket_t s, void* buffer, size_t size);
long socketSend(socket_t s, const void* data, size_t size);
long socketSendTo(socket_t s, const void* data, size_t size, const sockaddr_in& to);

bool socketSetReceiveTimeout(socket_t s, int milliseconds);
bool socketSetSendTimeout(socket_t s, int milliseconds);
inline bool socketSetTimeouts(socket_t s, int milliseconds)
{
    return socketSetReceiveTimeout(s, milliseconds) && socketSetSendTimeout(s, milliseconds);
}
bool socketSetNonBlocking(socket_t s);
bool socketSetReuseAddress(socket_t s);

/// Ожидание готовности: 1 — готов, 0 — таймаут, -1 — ошибка.
int socketPoll(socket_t s, bool forWrite, int timeoutMs);
}
