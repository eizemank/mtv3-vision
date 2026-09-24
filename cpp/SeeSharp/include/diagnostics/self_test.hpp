#pragma once

// Самотесты режима разработчика (web UI → GET /dev/tests, POST /dev/tests/run).
// Выполняются внутри работающего mainCV на устройстве: node/g++ на плате
// не нужны. Виды: unit (чистый код), live (состояние работающей программы),
// hardware (шлёт байты в UART — только с явным разрешением).

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace self_test
{
struct UartPort
{
    std::string device;      // пусто: UART-транспорт не работает
    std::string protocol;    // "dxl" | "binary"
    int baud = 0;
};

struct Context
{
    int httpPort = 8081;
    std::function<UartPort()> uartPort;
    nlohmann::json config;          // применённый конфиг (порт, если транспорт не открыт)
    bool allowHardware = false;
};

// {"tests":[{"id","title","kind"}]}
nlohmann::json list();
// ids пусто — все; {"results":[{"id","title","kind","status","message","duration_ms"}],
//  "passed","failed","skipped"}
nlohmann::json run(const std::vector<std::string>& ids, const Context& context);
}
