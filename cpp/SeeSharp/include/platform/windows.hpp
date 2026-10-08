#pragma once
// Единая точка подключения Win32/WinSock2: winsock2.h обязан идти раньше
// windows.h, иначе конфликт с winsock.h. Все платформенные файлы включают
// только этот заголовок. На POSIX заголовок пустой.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <sys/types.h>   // ssize_t (MinGW-w64)
#endif
