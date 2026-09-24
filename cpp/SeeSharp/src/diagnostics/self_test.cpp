#include "diagnostics/self_test.hpp"
#include "diagnostics/uart_unit_tests.hpp"
#include "transport/uart_rx_log.hpp"
#include "transport/uart_tx_log.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

namespace self_test
{
namespace
{
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Outcome
{
    std::string status, message;
};

Outcome pass(std::string message) { return {"PASS", std::move(message)}; }
Outcome fail(std::string message) { return {"FAIL", std::move(message)}; }
Outcome skip(std::string message) { return {"SKIP", std::move(message)}; }

std::string errnoText(const char* call)
{
    return std::string(call) + ": " + std::strerror(errno);
}

std::string canonical(const std::string& device)
{
    std::error_code error;
    const auto path = fs::canonical(device, error);
    return error ? device : path.string();
}

std::string lastError(UartRxLog& log)
{
    const auto snapshot = log.snapshot();
    return snapshot.lastError.empty() ? "" : "; last error: " + snapshot.lastError;
}

std::string lastEntryContaining(UartRxLog& log, const std::string& needle)
{
    const auto snapshot = log.snapshot();
    for (auto entry = snapshot.entries.rbegin(); entry != snapshot.entries.rend(); ++entry)
        if (entry->detail.find(needle) != std::string::npos)
            return entry->detail;
    return "";
}

// Без транспорта: берём порт из конфига (включённый — в приоритете)
UartPort configuredPort(const nlohmann::json& config)
{
    const auto transports = config.value("transports", nlohmann::json::object());
    UartPort fallback;
    for (const char* name : {"uart_dxl", "uart_binary"})
    {
        const auto section = transports.value(name, nlohmann::json::object());
        if (!section.contains("device"))
            continue;
        UartPort port{section.value("device", ""),
                      name == std::string("uart_dxl") ? "dxl" : "binary",
                      section.value("baud", 115200)};
        if (section.value("enabled", false))
            return port;
        if (fallback.device.empty())
            fallback = port;
    }
    return fallback;
}

speed_t baudConstant(int baud)
{
    switch (baud)
    {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
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

std::string readFile(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    std::stringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// --- live ---

bool httpGet(int port, const std::string& path, std::string& body, std::string& error)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        error = errnoText("socket");
        return false;
    }
    timeval timeout{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        error = errnoText("connect");
        close(fd);
        return false;
    }
    const std::string request = "GET " + path + " HTTP/1.0\r\nHost: localhost\r\n\r\n";
    send(fd, request.data(), request.size(), MSG_NOSIGNAL);
    std::string response;
    char chunk[4096];
    ssize_t count;
    while ((count = read(fd, chunk, sizeof(chunk))) > 0)
        response.append(chunk, static_cast<size_t>(count));
    close(fd);
    const size_t split = response.find("\r\n\r\n");
    if (split == std::string::npos)
    {
        error = "no HTTP response";
        return false;
    }
    body = response.substr(split + 4);
    return true;
}

Outcome logEndpoints(const Context& context)
{
    std::string summary;
    for (const char* path : {"/uart/rx-log", "/uart/tx-log"})
    {
        std::string body, error;
        if (!httpGet(context.httpPort, path, body, error))
            return fail(std::string(path) + ": " + error);
        nlohmann::json log;
        try
        {
            log = nlohmann::json::parse(body);
        }
        catch (const std::exception&)
        {
            return fail(std::string(path) + " did not return JSON (route not matched?): " +
                        body.substr(0, 40));
        }
        if (!log.contains("entries") || !log["entries"].is_array() || !log.contains("state"))
            return fail(std::string(path) + ": JSON without state/entries");
        summary += std::string(summary.empty() ? "" : "; ") + path + ": " +
                   log["state"].get<std::string>();
    }
    return pass(summary);
}

// Кто ещё держит порт: getty, minicom, второй mainCV...
std::string otherPortUsers(const std::string& device)
{
    std::string users;
    const std::string self = std::to_string(getpid());
    std::error_code error;
    for (const auto& process : fs::directory_iterator("/proc", error))
    {
        const std::string pid = process.path().filename().string();
        if (pid.empty() || !std::all_of(pid.begin(), pid.end(), ::isdigit) || pid == self)
            continue;
        std::error_code fdError;
        for (const auto& fd : fs::directory_iterator(process.path() / "fd", fdError))
        {
            std::error_code linkError;
            if (fs::read_symlink(fd.path(), linkError).string() != device)
                continue;
            std::string name = readFile(process.path() / "comm");
            name.erase(std::remove(name.begin(), name.end(), '\n'), name.end());
            users += (users.empty() ? "" : ", ") + name + " (pid " + pid + ")";
            break;
        }
    }
    return users;
}

Outcome portCheck(const Context& context)
{
    const UartPort port = context.uartPort ? context.uartPort() : UartPort{};
    if (port.device.empty())
        return fail("no UART transport is running: " + UartTxLog::instance().snapshot().state +
                    lastError(UartTxLog::instance()));
    const std::string real = canonical(port.device);
    struct stat info{};
    if (stat(real.c_str(), &info) != 0 || !S_ISCHR(info.st_mode))
        return fail(port.device + " is not a character device");

    const std::string name = fs::path(real).filename().string();
    std::string cmdline = readFile("/proc/cmdline");
    for (const std::string& console : {"console=" + name, std::string("console=serial0")})
        if (cmdline.find(console) != std::string::npos &&
            (console != "console=serial0" || canonical("/dev/serial0") == real))
            return fail("kernel console uses " + real + " (" + console +
                        " in /proc/cmdline): run board/raspberry-cm5/setup_uart.sh and reboot");
    const std::string users = otherPortUsers(real);
    if (!users.empty())
        return fail(real + " is also open by " + users +
                    ": the other reader steals RX bytes (serial-getty? minicom?)");
    return pass(port.protocol + " on " + port.device +
                (real != port.device ? " -> " + real : "") + " @ " +
                std::to_string(port.baud) + ", no other users of the port");
}

Outcome txActivity(const Context& context)
{
    const UartPort port = context.uartPort ? context.uartPort() : UartPort{};
    if (port.device.empty())
        return fail("no UART transport is running: " + UartTxLog::instance().snapshot().state +
                    lastError(UartTxLog::instance()));
    auto& log = UartTxLog::instance();
    const bool dxl = port.protocol == "dxl";
    const auto sent = [&] {
        return dxl ? log.counter("push_sent") + log.counter("responses_sent")
                   : log.counter("frames_sent");
    };
    const uint64_t published0 = log.counter("frames_published");
    const uint64_t dropped0 = log.counter("frames_dropped_busy");
    const uint64_t sent0 = sent();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    const uint64_t published = log.counter("frames_published") - published0;
    const uint64_t dropped = log.counter("frames_dropped_busy") - dropped0;
    const uint64_t written = sent() - sent0;
    const std::string errors = "write_errors=" + std::to_string(log.counter("write_errors")) +
                               lastError(log);
    if (dxl)
    {
        if (written)
            return pass(std::to_string(written) + " DXL packet(s) written in 1.5 s");
        const std::string reason = lastEntryContaining(log, "] push ");
        return fail("no DXL packets written in 1.5 s; " +
                    (reason.empty() ? std::string("push state unknown") : reason) + "; " + errors);
    }
    if (written)
        return pass(std::to_string(written) + " frame(s) written in 1.5 s (published " +
                    std::to_string(published) + ", replaced before sending " +
                    std::to_string(dropped) + ")");
    if (!published)
        return fail("the pipeline published no frames: processing_mode off or no camera frames");
    return fail(std::to_string(published) + " frame(s) published but none written; " + errors);
}

// --- hardware ---

Outcome loopback(const Context& context)
{
    if (!context.allowHardware)
        return skip("hardware tests are off: enable them to send 12 test bytes to UART TX");
    UartPort port = context.uartPort ? context.uartPort() : UartPort{};
    const bool active = !port.device.empty();
    if (!active)
        port = configuredPort(context.config);
    if (port.device.empty())
        return fail("no UART device in transports.uart_dxl / uart_binary");

    // SSLB + случайные байты без AA/55/FF: ни один парсер не примет их за пакет
    std::vector<uint8_t> marker{'S', 'S', 'L', 'B'};
    std::random_device random;
    while (marker.size() < 12)
    {
        const uint8_t value = static_cast<uint8_t>(random());
        if (value != 0x00 && value != 0xAA && value != 0x55 && value != 0xFF)
            marker.push_back(value);
    }

    // Порт уже открыт транспортом: его termios не трогаем, а RX читает он —
    // маркер ищем в журнале RX. Иначе настраиваем и читаем порт сами.
    const int fd = open(port.device.c_str(),
                        active ? (O_WRONLY | O_NOCTTY) : (O_RDWR | O_NOCTTY | O_NONBLOCK));
    if (fd < 0)
        return fail(port.device + ": " + errnoText("open"));
    struct Closer { int fd; ~Closer() { close(fd); } } closer{fd};
    if (!active)
    {
        const speed_t speed = baudConstant(port.baud);
        termios options{};
        if (!speed || tcgetattr(fd, &options) != 0)
            return fail(port.device + ": cannot configure baud " + std::to_string(port.baud));
        cfmakeraw(&options);
        cfsetispeed(&options, speed);
        cfsetospeed(&options, speed);
        options.c_cflag &= ~(CSTOPB | CRTSCTS);
        options.c_cflag |= CLOCAL | CREAD;
        if (tcsetattr(fd, TCSANOW, &options) != 0)
            return fail(port.device + ": " + errnoText("tcsetattr"));
        tcflush(fd, TCIFLUSH);
    }

    auto& rx = UartRxLog::instance();
    uint64_t startSequence = 0;
    for (const auto& entry : rx.snapshot().entries)
        startSequence = std::max(startSequence, entry.sequence);
    const uint64_t echoed0 = rx.counter("rx_frames_ok");

    const auto started = Clock::now();
    if (write(fd, marker.data(), marker.size()) != static_cast<ssize_t>(marker.size()))
        return fail(port.device + ": " + errnoText("write"));
    tcdrain(fd);

    const std::string markerHex = UartRxLog::toHex(marker.data(), marker.size());
    std::vector<uint8_t> received;
    size_t otherBytes = 0;
    while (Clock::now() - started < std::chrono::seconds(1))
    {
        bool found = false;
        if (active)
        {
            // маркер может прийти двумя кусками — склеиваем сырые записи
            std::string stream;
            otherBytes = 0;
            for (const auto& entry : rx.snapshot().entries)
                if (entry.sequence > startSequence && entry.kind == "BYTES")
                {
                    stream += (stream.empty() ? "" : " ") + entry.hex;
                    otherBytes += (entry.hex.size() + 1) / 3;
                }
            found = stream.find(markerHex) != std::string::npos;
        }
        else
        {
            uint8_t chunk[256];
            const ssize_t count = read(fd, chunk, sizeof(chunk));
            if (count > 0)
                received.insert(received.end(), chunk, chunk + count);
            found = std::search(received.begin(), received.end(),
                                marker.begin(), marker.end()) != received.end();
            otherBytes = received.size();
        }
        if (found)
        {
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - started).count();
            std::string message = "12-byte marker came back on RX after " +
                std::to_string(ms) + " ms (" +
                (active ? port.protocol + " transport RX" : std::string("direct read")) +
                ", " + port.device + " @ " + std::to_string(port.baud) + ")";
            const uint64_t echoed = rx.counter("rx_frames_ok") - echoed0;
            if (active && port.protocol == "binary" && echoed)
                message += "; binary frames echoed with valid CRC: " + std::to_string(echoed);
            return pass(message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return fail("marker not received within 1 s on " + port.device + " (" +
                std::to_string(otherBytes) + " other byte(s) received meanwhile). "
                "Check the TX-RX jumper (GPIO14 pin 8 <-> GPIO15 pin 10), "
                "dtoverlay=uart0-pi5 and that no other process reads the port");
}

struct Test
{
    std::string id, title, kind;
    std::function<Outcome(const Context&)> run;
};

const std::vector<Test>& tests()
{
    static const std::vector<Test> all = [] {
        std::vector<Test> list;
        for (const auto& unit : uart_unit_tests::all())
            list.push_back({unit.id, unit.title, "unit", [run = unit.run](const Context&) {
                const std::string error = run();
                return error.empty() ? pass("ok") : fail(error);
            }});
        list.push_back({"web.uart_logs", "UART log endpoints return JSON", "live", logEndpoints});
        list.push_back({"uart.port", "UART port open, no console/getty/other readers", "live", portCheck});
        list.push_back({"uart.tx_activity", "UART TX sends packets (1.5 s)", "live", txActivity});
        list.push_back({"uart.loopback", "TX->RX loopback with a jumper (sends 12 bytes)", "hardware", loopback});
        return list;
    }();
    return all;
}
}

nlohmann::json list()
{
    nlohmann::json items = nlohmann::json::array();
    for (const auto& test : tests())
        items.push_back({{"id", test.id}, {"title", test.title}, {"kind", test.kind}});
    return {{"tests", items}};
}

nlohmann::json run(const std::vector<std::string>& ids, const Context& context)
{
    nlohmann::json results = nlohmann::json::array();
    int passed = 0, failed = 0, skipped = 0;
    for (const auto& test : tests())
    {
        if (!ids.empty() && std::find(ids.begin(), ids.end(), test.id) == ids.end())
            continue;
        const auto started = Clock::now();
        Outcome outcome;
        try
        {
            outcome = test.run(context);
        }
        catch (const std::exception& error)
        {
            outcome = fail(std::string("exception: ") + error.what());
        }
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        (outcome.status == "PASS" ? passed : outcome.status == "SKIP" ? skipped : failed)++;
        results.push_back({{"id", test.id}, {"title", test.title}, {"kind", test.kind},
                           {"status", outcome.status}, {"message", outcome.message},
                           {"duration_ms", std::round(ms * 10) / 10}});
    }
    return {{"results", results}, {"passed", passed}, {"failed", failed}, {"skipped", skipped}};
}
}
