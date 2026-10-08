// Host test of the UART protocols and the binary transport on a pty pair.
// Build and run from cpp/SeeSharp (see README.md, "Tests"):
//   g++ -std=c++17 -I include -I third_party -I /usr/include/opencv4
//       tests/uart_protocol.test.cpp src/transport/binary_uart_transport.cpp
//       src/transport/dxl_uart_transport.cpp src/platform/serial.cpp
//       -lopencv_core -pthread -lutil -o /tmp/uart-protocol-test
//   (or: sh board/host/run_tests.sh from the repository root)
//   /tmp/uart-protocol-test
// The same pure checks run on the device from the web UI developer mode.
#include "diagnostics/uart_unit_tests.hpp"
#include "transport/binary_uart_transport.hpp"
#include "transport/dxl_uart_transport.hpp"
#include "transport/uart_rx_log.hpp"
#include "transport/uart_tx_log.hpp"

#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

namespace
{
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << std::endl;
    }
}

bool waitFor(const std::function<bool()>& condition)
{
    // условие может быть одноразовым (parser.next) — вызываем его ровно до успеха
    for (int i = 0; i < 200; ++i)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void binaryTransportOnPty()
{
    int master = -1, slave = -1;
    char name[128];
    check(openpty(&master, &slave, name, nullptr, nullptr) == 0, "openpty");
    {
        BinaryUartTransport transport(name, 115200, 2);
        check(transport.isOpen(), "binary transport did not open the pty");

        transport.publish(uart_unit_tests::sampleFrame(3));
        binary_uart::Parser parser;
        binary_uart::ParsedPacket packet;
        const bool got = waitFor([&] {
            uint8_t chunk[256];
            const int flags = fcntl(master, F_GETFL);
            fcntl(master, F_SETFL, flags | O_NONBLOCK);
            const ssize_t count = read(master, chunk, sizeof(chunk));
            if (count > 0)
                parser.feed(chunk, static_cast<size_t>(count));
            return parser.next(packet);
        });
        check(got && packet.crcOk && packet.objects == 2,
              "frame written to the pty is not a valid packet with 2 objects");
        check(UartTxLog::instance().counter("frames_sent") == 1, "frames_sent counter");

        // Петля: то, что «пришло» на RX, видно в журнале RX как PACKET
        const auto echo = binary_uart::buildPacket(uart_unit_tests::sampleFrame(1), 20);
        check(write(master, echo.data(), echo.size()) == static_cast<ssize_t>(echo.size()),
              "write to pty master");
        check(waitFor([] { return UartRxLog::instance().counter("rx_frames_ok") == 1; }),
              "RX monitor did not log the echoed frame");
    }
    check(UartTxLog::instance().snapshot().state == "Binary TX stopped", "stopped state");
    close(master);
    close(slave);
}

// Регрессия: 1–5 байт шума на RX навсегда останавливали DXL push
void dxlPushSurvivesNoise()
{
    int master = -1, slave = -1;
    char name[128];
    check(openpty(&master, &slave, name, nullptr, nullptr) == 0, "openpty");
    const std::string eeprom = "/tmp/uart-protocol-test-" + std::to_string(getpid()) + ".bin";
    {
        DxlUartTransport transport(name, 115200, 100, false, eeprom, nullptr, true, 10);
        check(transport.isOpen(), "DXL transport did not open the pty");
        transport.publish(uart_unit_tests::sampleFrame(1));
        check(waitFor([] { return UartTxLog::instance().counter("push_sent") > 0; }),
              "DXL push did not start");
        const uint8_t noise[] = {0x12, 0x34, 0xFF};
        check(write(master, noise, sizeof(noise)) == 3, "write noise");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const uint64_t before = UartTxLog::instance().counter("push_sent");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        check(UartTxLog::instance().counter("push_sent") > before + 5,
              "DXL push stopped after RX noise");
        check(UartRxLog::instance().counter("rx_discarded_bytes") == 3,
              "noise bytes are not counted as discarded");
    }
    unlink(eeprom.c_str());
    close(master);
    close(slave);
}

void binaryTransportOpenFailures()
{
    UartTxLog::instance().resetCounters();
    BinaryUartTransport missing("/dev/does-not-exist", 115200, 20);
    check(!missing.isOpen(), "missing device reported as open");
    check(UartTxLog::instance().snapshot().lastError.find("No such file") != std::string::npos,
          "open() errno is not in last_error");

    // Регрессия: tcsetattr/tcgetattr на не-tty оставлял fd открытым => isOpen()
    BinaryUartTransport notTty("/dev/null", 115200, 20);
    check(!notTty.isOpen(), "non-tty device must be reported as closed");

    BinaryUartTransport badBaud("/dev/null", 12345, 20);
    check(!badBaud.isOpen(), "unsupported baud must fail");
    check(UartTxLog::instance().snapshot().lastError.find("unsupported baud") != std::string::npos,
          "unsupported baud is not explained");
}
}

int main()
{
    for (const auto& test : uart_unit_tests::all())
    {
        const std::string error = test.run();
        check(error.empty(), std::string(test.id) + ": " + error);
    }
    binaryTransportOpenFailures();
    binaryTransportOnPty();
    UartRxLog::instance().resetCounters();
    UartTxLog::instance().resetCounters();
    dxlPushSurvivesNoise();
    if (failures)
    {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "uart protocol tests: PASS" << std::endl;
    return 0;
}
