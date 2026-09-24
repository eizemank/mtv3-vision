#include "training/trainer_job.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <future>

#include <fcntl.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_CLASS_IDLE) без linux/ioprio.h
void setIdleIoPriority()
{
    constexpr int kWhoProcess = 1;
    constexpr int kClassIdle = 3;
    constexpr int kClassShift = 13;
    syscall(SYS_ioprio_set, kWhoProcess, 0, kClassIdle << kClassShift);
}
}  // namespace

TrainerJob::~TrainerJob()
{
    stop();
    if (reader_.joinable())
        reader_.join();
}

pid_t TrainerJob::spawn(const std::vector<std::string>& argv,
                        const std::vector<std::string>& extraEnv,
                        const std::string& workdir, bool lowPriority, int& outFd,
                        std::string& error)
{
    if (argv.empty())
    {
        error = "empty command";
        return -1;
    }
    int pipeFd[2];
    if (pipe(pipeFd) != 0)
    {
        error = std::string("pipe: ") + strerror(errno);
        return -1;
    }
    // argv/envp готовим до fork: после него в потомке только async-signal-safe
    std::vector<char*> args;
    for (const std::string& arg : argv)
        args.push_back(const_cast<char*>(arg.c_str()));
    args.push_back(nullptr);
    std::vector<std::string> envStorage;
    for (char** e = environ; e && *e; ++e)
        envStorage.emplace_back(*e);
    for (const std::string& extra : extraEnv)
    {
        const std::string key = extra.substr(0, extra.find('='));
        for (auto it = envStorage.begin(); it != envStorage.end();)
            it = it->compare(0, key.size() + 1, key + "=") == 0 ? envStorage.erase(it) : it + 1;
        envStorage.push_back(extra);
    }
    std::vector<char*> envp;
    for (const std::string& e : envStorage)
        envp.push_back(const_cast<char*>(e.c_str()));
    envp.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0)
    {
        error = std::string("fork: ") + strerror(errno);
        close(pipeFd[0]);
        close(pipeFd[1]);
        return -1;
    }
    if (pid == 0)
    {
        close(pipeFd[0]);
        dup2(pipeFd[1], STDOUT_FILENO);
        dup2(pipeFd[1], STDERR_FILENO);
        close(pipeFd[1]);
        int devNull = open("/dev/null", O_RDONLY);
        if (devNull >= 0)
        {
            dup2(devNull, STDIN_FILENO);
            close(devNull);
        }
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (lowPriority)
        {
            setpriority(PRIO_PROCESS, 0, 15);
            setIdleIoPriority();
        }
        if (!workdir.empty() && chdir(workdir.c_str()) != 0)
            _exit(126);
        execve(args[0], args.data(), envp.data());
        // execve не ищет в PATH — пробуем execvpe-подобный обход
        const char* path = getenv("PATH");
        if (path && !strchr(args[0], '/'))
        {
            std::string dirs = path;
            size_t start = 0;
            while (start <= dirs.size())
            {
                size_t end = dirs.find(':', start);
                if (end == std::string::npos)
                    end = dirs.size();
                std::string candidate = dirs.substr(start, end - start) + "/" + args[0];
                execve(candidate.c_str(), args.data(), envp.data());
                start = end + 1;
            }
        }
        _exit(127);
    }
    close(pipeFd[1]);
    outFd = pipeFd[0];
    return pid;
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
    // fork делаем из потока-читателя: PR_SET_PDEATHSIG привязан к потоку-
    // родителю, а HTTP-поток, обслуживающий запрос, живёт недолго.
    std::promise<std::string> spawned;
    std::future<std::string> spawnResult = spawned.get_future();
    reader_ = std::thread([this, argv, extraEnv, workdir,
                           spawned = std::move(spawned)]() mutable {
        int fd = -1;
        std::string spawnError;
        const pid_t pid = spawn(argv, extraEnv, workdir, true, fd, spawnError);
        if (pid < 0)
        {
            spawned.set_value(spawnError.empty() ? "spawn failed" : spawnError);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pid_ = pid;
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
        readerLoop(fd);
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
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
        stopRequested_ = true;
        pid = pid_;
    }
    kill(pid, SIGTERM);
    for (int i = 0; i < 50; ++i)
    {
        usleep(100 * 1000);
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_)
            return;
    }
    kill(pid, SIGKILL);
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

void TrainerJob::readerLoop(int fd)
{
    std::string pending;
    char buffer[4096];
    for (;;)
    {
        const ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got < 0 && errno == EINTR)
            continue;
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
    close(fd);

    int status = 0;
    pid_t pid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pid = pid_;
    }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
    finishedMs_ = nowMs();
    if (WIFEXITED(status))
        exitCode_ = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        exitCode_ = 128 + WTERMSIG(status);
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
    int fd = -1;
    exitCode = -1;
    const pid_t pid = spawn(argv, {"PYTHONUNBUFFERED=1"}, "", false, fd, error);
    if (pid < 0)
        return error;
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    for (;;)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0)
        {
            timedOut = true;
            break;
        }
        pollfd pfd{fd, POLLIN, 0};
        const int ready = poll(&pfd, 1, static_cast<int>(left));
        if (ready < 0 && errno == EINTR)
            continue;
        if (ready == 0)
        {
            timedOut = true;
            break;
        }
        char buffer[4096];
        const ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got <= 0)
            break;
        if (output.size() < 64 * 1024)
            output.append(buffer, static_cast<size_t>(got));
    }
    close(fd);
    if (timedOut)
    {
        kill(pid, SIGKILL);
        output += "\n[timeout after " + std::to_string(timeoutMs) + " ms]";
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (!timedOut && WIFEXITED(status))
        exitCode = WEXITSTATUS(status);
    return output;
}
