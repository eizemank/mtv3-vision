#pragma once
// TrainerJob — запуск python-тренера подпроцессом: пониженный приоритет
// (nice/ioprio idle), смерть вместе с родителем (PR_SET_PDEATHSIG), чтение
// stdout/stderr отдельным потоком с разбором строк
//   PROGRESS {json}  -> status().progress
//   RESULT   {json}  -> status().result
// остальное — в кольцевой лог (300 строк) с монотонным sequence для поллинга.

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "platform/process.hpp"

class TrainerJob
{
public:
    struct LogEntry
    {
        uint64_t sequence = 0;
        int64_t timeMs = 0;
        std::string line;
    };

    TrainerJob() = default;
    ~TrainerJob();
    TrainerJob(const TrainerJob&) = delete;
    TrainerJob& operator=(const TrainerJob&) = delete;

    /// Запуск; false + error, если уже идёт или процесс не запустился.
    bool start(const std::vector<std::string>& argv,
               const std::vector<std::string>& extraEnv,
               const std::string& workdir, std::string& error);
    /// SIGTERM (Windows: TerminateProcess), до 5 с ожидания, затем SIGKILL.
    void stop();
    bool running() const;

    /// {state, pid, started_ms, finished_ms, exit_code, progress, result, error}
    nlohmann::json status() const;
    /// Строки лога с sequence > since (для поллинга ?since=N).
    nlohmann::json log(uint64_t since) const;

    /// Одноразовый запуск с ожиданием (проба python/torch): вывод + код выхода.
    /// timeoutMs истёк -> процесс убивается, exitCode = -1.
    static std::string runCapture(const std::vector<std::string>& argv, int timeoutMs,
                                  int& exitCode);

private:
    void readerLoop(platform::ChildProcess& child);
    void consumeLine(const std::string& line);
    void appendLog(const std::string& line);

    mutable std::mutex mutex_;
    std::thread reader_;
    std::shared_ptr<platform::ChildProcess> child_;
    long long pid_ = -1;
    bool running_ = false;
    std::string state_ = "idle";
    std::string error_;
    int exitCode_ = -1;
    int64_t startedMs_ = 0;
    int64_t finishedMs_ = 0;
    nlohmann::json progress_ = nlohmann::json::object();
    nlohmann::json result_ = nlohmann::json::object();
    std::deque<LogEntry> log_;
    uint64_t logSequence_ = 0;
    bool stopRequested_ = false;
    static constexpr size_t kMaxLogLines = 300;
};
