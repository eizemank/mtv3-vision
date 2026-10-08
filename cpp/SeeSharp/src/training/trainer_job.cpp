#include "training/trainer_job.hpp"

#include <chrono>
#include <future>
#include <thread>

namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}
}  // namespace

TrainerJob::~TrainerJob()
{
    stop();
    if (reader_.joinable())
        reader_.join();
}

bool TrainerJob::start(const std::vector<std::string>& argv,
                       const std::vector<std::string>& extraEnv,
                       const std::string& workdir, std::string& error)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_)
        {
            error = "training already running";
            return false;
        }
    }
    if (reader_.joinable())
        reader_.join();
    // Запуск делаем из потока-читателя: PR_SET_PDEATHSIG привязан к потоку-
    // родителю, а HTTP-поток, обслуживающий запрос, живёт недолго.
    std::promise<std::string> spawned;
    std::future<std::string> spawnResult = spawned.get_future();
    reader_ = std::thread([this, argv, extraEnv, workdir,
                           spawned = std::move(spawned)]() mutable {
        auto child = std::make_shared<platform::ChildProcess>();
        std::string spawnError;
        if (!child->start(argv, extraEnv, workdir, true, spawnError))
        {
            spawned.set_value(spawnError.empty() ? "spawn failed" : spawnError);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            child_ = child;
            pid_ = child->pid();
            running_ = true;
            stopRequested_ = false;
            state_ = "running";
            error_.clear();
            exitCode_ = -1;
            startedMs_ = nowMs();
            finishedMs_ = 0;
            progress_ = nlohmann::json::object();
            result_ = nlohmann::json::object();
            log_.clear();
            std::string command;
            for (const std::string& arg : argv)
                command += (command.empty() ? "" : " ") + arg;
            ++logSequence_;
            log_.push_back({logSequence_, startedMs_, "$ " + command});
        }
        spawned.set_value("");
        readerLoop(*child);
    });
    error = spawnResult.get();
    if (!error.empty())
    {
        reader_.join();
        return false;
    }
    return true;
}

void TrainerJob::stop()
{
    std::shared_ptr<platform::ChildProcess> child;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
        stopRequested_ = true;
        child = child_;
    }
    if (!child)
        return;
    child->terminate();
    for (int i = 0; i < 50; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
    }
    child->kill();
}

bool TrainerJob::running() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

void TrainerJob::appendLog(const std::string& line)
{
    ++logSequence_;
    log_.push_back({logSequence_, nowMs(), line});
    while (log_.size() > kMaxLogLines)
        log_.pop_front();
}

void TrainerJob::consumeLine(const std::string& line)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (line.compare(0, 9, "PROGRESS ") == 0)
    {
        try
        {
            progress_ = nlohmann::json::parse(line.substr(9));
            return;
        }
        catch (...) {}
    }
    else if (line.compare(0, 7, "RESULT ") == 0)
    {
        try
        {
            result_ = nlohmann::json::parse(line.substr(7));
            return;
        }
        catch (...) {}
    }
    appendLog(line);
}

void TrainerJob::readerLoop(platform::ChildProcess& child)
{
    std::string pending;
    char buffer[4096];
    for (;;)
    {
        const long got = child.readOutput(buffer, sizeof(buffer));
        if (got <= 0)
            break;
        pending.append(buffer, static_cast<size_t>(got));
        size_t newline;
        while ((newline = pending.find('\n')) != std::string::npos)
        {
            std::string line = pending.substr(0, newline);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            pending.erase(0, newline + 1);
            if (!line.empty())
                consumeLine(line);
        }
    }
    if (!pending.empty())
        consumeLine(pending);
    child.closeOutput();

    const int exitCode = child.wait();
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    child_.reset();
    finishedMs_ = nowMs();
    exitCode_ = exitCode;
    if (stopRequested_)
        state_ = "stopped";
    else if (exitCode_ == 0)
        state_ = "done";
    else
    {
        state_ = "failed";
        error_ = exitCode_ == 127 ? "trainer executable not found"
               : exitCode_ == 126 ? "cannot enter working directory"
               : exitCode_ == 3 ? "PyTorch is not installed"
               : "exit code " + std::to_string(exitCode_);
    }
    appendLog("[exit " + std::to_string(exitCode_) + ", " + state_ + "]");
}

nlohmann::json TrainerJob::status() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return {{"state", state_}, {"pid", running_ ? pid_ : 0}, {"started_ms", startedMs_},
            {"finished_ms", finishedMs_}, {"exit_code", exitCode_}, {"progress", progress_},
            {"result", result_}, {"error", error_}, {"log_sequence", logSequence_}};
}

nlohmann::json TrainerJob::log(uint64_t since) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json entries = nlohmann::json::array();
    for (const LogEntry& entry : log_)
        if (entry.sequence > since)
            entries.push_back({{"sequence", entry.sequence}, {"time_ms", entry.timeMs},
                               {"line", entry.line}});
    return {{"sequence", logSequence_}, {"state", state_}, {"entries", entries}};
}

std::string TrainerJob::runCapture(const std::vector<std::string>& argv, int timeoutMs,
                                   int& exitCode)
{
    std::string error;
    exitCode = -1;
    platform::ChildProcess child;
    if (!child.start(argv, {"PYTHONUNBUFFERED=1"}, "", false, error))
        return error;
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    for (;;)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0 || !child.waitOutput(static_cast<int>(left)))
        {
            timedOut = true;
            break;
        }
        char buffer[4096];
        const long got = child.readOutput(buffer, sizeof(buffer));
        if (got <= 0)
            break;
        if (output.size() < 64 * 1024)
            output.append(buffer, static_cast<size_t>(got));
    }
    child.closeOutput();
    if (timedOut)
    {
        child.kill();
        output += "\n[timeout after " + std::to_string(timeoutMs) + " ms]";
    }
    const int code = child.wait();
    if (!timedOut)
        exitCode = code;
    return output;
}
