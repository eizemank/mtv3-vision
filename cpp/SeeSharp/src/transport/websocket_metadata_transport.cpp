#include "transport/websocket_metadata_transport.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "transport/metadata_json.hpp"

namespace
{
uint32_t rotateLeft(uint32_t value, unsigned bits)
{
    return (value << bits) | (value >> (32 - bits));
}

std::array<uint8_t, 20> sha1(const std::string& input)
{
    std::vector<uint8_t> data(input.begin(), input.end());
    const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56)
        data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8)
        data.push_back(static_cast<uint8_t>(bitLength >> shift));

    uint32_t h0 = 0x67452301;
    uint32_t h1 = 0xEFCDAB89;
    uint32_t h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476;
    uint32_t h4 = 0xC3D2E1F0;
    for (size_t offset = 0; offset < data.size(); offset += 64)
    {
        uint32_t words[80]{};
        for (size_t index = 0; index < 16; ++index)
        {
            const size_t position = offset + index * 4;
            words[index] = (static_cast<uint32_t>(data[position]) << 24) |
                           (static_cast<uint32_t>(data[position + 1]) << 16) |
                           (static_cast<uint32_t>(data[position + 2]) << 8) |
                           data[position + 3];
        }
        for (size_t index = 16; index < 80; ++index)
            words[index] = rotateLeft(words[index - 3] ^ words[index - 8] ^
                                      words[index - 14] ^ words[index - 16], 1);

        uint32_t a = h0;
        uint32_t b = h1;
        uint32_t c = h2;
        uint32_t d = h3;
        uint32_t e = h4;
        for (size_t index = 0; index < 80; ++index)
        {
            uint32_t function = 0;
            uint32_t constant = 0;
            if (index < 20)
            {
                function = (b & c) | ((~b) & d);
                constant = 0x5A827999;
            }
            else if (index < 40)
            {
                function = b ^ c ^ d;
                constant = 0x6ED9EBA1;
            }
            else if (index < 60)
            {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8F1BBCDC;
            }
            else
            {
                function = b ^ c ^ d;
                constant = 0xCA62C1D6;
            }
            const uint32_t temporary = rotateLeft(a, 5) + function + e +
                                       constant + words[index];
            e = d;
            d = c;
            c = rotateLeft(b, 30);
            b = a;
            a = temporary;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> digest{};
    const uint32_t hash[] = {h0, h1, h2, h3, h4};
    for (size_t index = 0; index < 5; ++index)
        for (size_t byte = 0; byte < 4; ++byte)
            digest[index * 4 + byte] = static_cast<uint8_t>(
                hash[index] >> (24 - byte * 8));
    return digest;
}

std::string base64(const std::array<uint8_t, 20>& input)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (size_t index = 0; index < input.size(); index += 3)
    {
        const uint32_t value = static_cast<uint32_t>(input[index]) << 16 |
            (index + 1 < input.size() ? static_cast<uint32_t>(input[index + 1]) << 8 : 0) |
            (index + 2 < input.size() ? input[index + 2] : 0);
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(index + 1 < input.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(index + 2 < input.size() ? alphabet[value & 63] : '=');
    }
    return output;
}

bool sendAll(int socketFd, const uint8_t* data, size_t size)
{
    size_t offset = 0;
    while (offset < size)
    {
        const ssize_t written = send(socketFd, data + offset, size - offset, MSG_NOSIGNAL);
        if (written <= 0)
            return false;
        offset += static_cast<size_t>(written);
    }
    return true;
}

bool performHandshake(int socketFd)
{
    timeval timeout{1, 0};
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socketFd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    std::string request;
    std::array<char, 2048> buffer{};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192)
    {
        const ssize_t count = recv(socketFd, buffer.data(), buffer.size(), 0);
        if (count <= 0)
            return false;
        request.append(buffer.data(), static_cast<size_t>(count));
    }
    const std::string header = "Sec-WebSocket-Key:";
    size_t position = request.find(header);
    if (position == std::string::npos)
        return false;
    position += header.size();
    while (position < request.size() && request[position] == ' ')
        ++position;
    const size_t end = request.find("\r\n", position);
    if (end == std::string::npos)
        return false;
    const std::string accept = base64(sha1(
        request.substr(position, end - position) +
        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
    const std::string response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    return sendAll(socketFd, reinterpret_cast<const uint8_t*>(response.data()),
                   response.size());
}

bool sendTextFrame(int socketFd, const std::string& payload)
{
    std::vector<uint8_t> frame;
    frame.reserve(payload.size() + 10);
    frame.push_back(0x81);
    if (payload.size() <= 125)
        frame.push_back(static_cast<uint8_t>(payload.size()));
    else if (payload.size() <= 65535)
    {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>(payload.size() >> 8));
        frame.push_back(static_cast<uint8_t>(payload.size()));
    }
    else
    {
        frame.push_back(127);
        const uint64_t size = payload.size();
        for (int shift = 56; shift >= 0; shift -= 8)
            frame.push_back(static_cast<uint8_t>(size >> shift));
    }
    frame.insert(frame.end(), payload.begin(), payload.end());
    return sendAll(socketFd, frame.data(), frame.size());
}
}

WebSocketMetadataTransport::WebSocketMetadataTransport(std::string bindAddress,
                                                       uint16_t port)
    : bindAddress_(std::move(bindAddress)), port_(port)
{
    listenerFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenerFd_ < 0)
        return;
    int reuse = 1;
    setsockopt(listenerFd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (bindAddress_.empty() || bindAddress_ == "0.0.0.0")
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    else if (inet_pton(AF_INET, bindAddress_.c_str(), &address.sin_addr) != 1)
        address.sin_addr.s_addr = INADDR_NONE;
    if (address.sin_addr.s_addr == INADDR_NONE ||
        bind(listenerFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listenerFd_, 8) != 0)
    {
        close(listenerFd_);
        listenerFd_ = -1;
        return;
    }
    fcntl(listenerFd_, F_SETFL, fcntl(listenerFd_, F_GETFL) | O_NONBLOCK);
    worker_ = std::thread(&WebSocketMetadataTransport::run, this);
    std::cout << "WebSocket metadata: ws://" << bindAddress_ << ':' << port_
              << std::endl;
}

WebSocketMetadataTransport::~WebSocketMetadataTransport()
{
    running_ = false;
    ready_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (listenerFd_ >= 0)
        close(listenerFd_);
}

void WebSocketMetadataTransport::publish(const VisionFrame& frame)
{
    if (listenerFd_ < 0)
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFrame_ = frame;
    hasFrame_ = true;
    ready_.notify_one();
}

void WebSocketMetadataTransport::run()
{
    std::vector<int> clients;
    while (running_)
    {
        while (true)
        {
            const int client = accept(listenerFd_, nullptr, nullptr);
            if (client < 0)
                break;
            if (performHandshake(client))
                clients.push_back(client);
            else
                close(client);
        }

        VisionFrame frame;
        bool sendFrame = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait_for(lock, std::chrono::milliseconds(20),
                            [this] { return hasFrame_ || !running_; });
            if (hasFrame_)
            {
                frame = std::move(pendingFrame_);
                hasFrame_ = false;
                sendFrame = true;
            }
        }
        if (!sendFrame || clients.empty())
            continue;
        const std::string payload = serializeVisionFrameJson(frame).dump();
        clients.erase(std::remove_if(clients.begin(), clients.end(),
            [&](int client) {
                if (sendTextFrame(client, payload))
                    return false;
                close(client);
                return true;
            }), clients.end());
    }
    for (int client : clients)
        close(client);
}
