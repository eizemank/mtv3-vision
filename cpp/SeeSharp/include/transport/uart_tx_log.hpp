#pragma once

#include "transport/uart_rx_log.hpp"

class UartTxLog : public UartRxLog
{
public:
    static UartTxLog& instance() { static UartTxLog log; return log; }

private:
    UartTxLog() : UartRxLog("UART TX disabled or unavailable in this build") {}
};
