#include "platform/process.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <system_error>
#include <thread>

#ifdef _WIN32
#include "platform/windows.hpp"
#include <cstdlib>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
extern char** environ;
#endif

namespace platform
{
ChildProcess::~ChildProcess()
{
    closeOutput();
    if (started() && !waited_)
    {
        kill();
        wait();
    }
#ifdef _WIN32
    if (process_)
        CloseHandle(static_cast<HANDLE>(process_));
    if (job_)
        CloseHandle(static_cast<HANDLE>(job_));
#endif
}

bool ChildProcess::started() const { return pid_ > 0; }

#ifdef _WIN32

namespace
{
std::string win32ErrorText(const char* call)
{
    const DWORD error = GetLastError();
    return std::string(call) + ": " + std::system_category().message(static_cast<int>(error)) +
           " (" + std::to_string(error) + ")";
}

// Кавычки по правилам CRT (CommandLineToArgv): обратные слэши перед кавычкой
// удваиваются, кавычка экранируется
std::string quoteArgument(const std::string& argument)
{
    if (!argument.empty() &&
        argument.find_first_of(" \t\n\v\"") == std::string::npos)
        return argument;
    std::string quoted = "\"";
    size_t backslashes = 0;
    for (char c : argument)
    {
        if (c == '\\')
        {
            ++backslashes;
            continue;
        }
        if (c == '"')
        {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
        }
        else
        {
            quoted.append(backslashes, '\\');
            quoted.push_back(c);
        }
        backslashes = 0;
    }
    quoted.append(backslashes * 2, '\\');
    quoted.push_back('"');
    return quoted;
}

// Блок окружения ANSI: KEY=VALUE\0...\0 с подменой ключей из extraEnv
std::string environmentBlock(const std::vector<std::string>& extraEnv)
{
    std::vector<std::string> entries;
    for (char** e = _environ; e && *e; ++e)
        entries.emplace_back(*e);
    for (const std::string& extra : extraEnv)
    {
        const std::string key = extra.substr(0, extra.find('=')) + "=";
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const std::string& e) {
            return e.size() >= key.size() &&
                   _strnicmp(e.c_str(), key.c_str(), key.size()) == 0;
        }), entries.end());
        entries.push_back(extra);
    }
    std::string block;
    for (const std::string& e : entries)
    {
        block += e;
        block.push_back('\0');
    }
    block.push_back('\0');
    return block;
}
}

bool ChildProcess::start(const std::vector<std::string>& argv,
                         const std::vector<std::string>& extraEnv,
                         const std::string& workdir, bool lowPriority, std::string& error)
{
    if (argv.empty())
    {
        error = "empty command";
        return false;
    }
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0))
    {
        error = win32ErrorText("CreatePipe");
        return false;
    }
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &inheritable, OPEN_EXISTING, 0, nullptr);

    std::string commandLine;
    for (const std::string& argument : argv)
        commandLine += (commandLine.empty() ? "" : " ") + quoteArgument(argument);
    std::string environment = environmentBlock(extraEnv);

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nul;
    startup.hStdOutput = writeEnd;
    startup.hStdError = writeEnd;
    PROCESS_INFORMATION info{};
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED;
    if (lowPriority)
        flags |= BELOW_NORMAL_PRIORITY_CLASS;
    const BOOL created = CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                                        flags, environment.data(),
                                        workdir.empty() ? nullptr : workdir.c_str(),
                                        &startup, &info);
    const std::string createError = created ? "" : win32ErrorText("CreateProcess");
    CloseHandle(writeEnd);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!created)
    {
        CloseHandle(readEnd);
        error = createError + " [" + argv[0] + "]";
        return false;
    }
    // Job Object: при закрытии последнего описателя (смерти родителя) потомок
    // уничтожается — аналог PR_SET_PDEATHSIG
    HANDLE job = CreateJobObjectA(nullptr, nullptr);
    if (job)
    {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        AssignProcessToJobObject(job, info.hProcess);
    }
    ResumeThread(info.hThread);
    CloseHandle(info.hThread);
    process_ = info.hProcess;
    job_ = job;
    output_ = readEnd;
    pid_ = static_cast<long long>(info.dwProcessId);
    waited_ = false;
    exitCode_ = -1;
    return true;
}

long ChildProcess::readOutput(void* buffer, size_t size)
{
    if (!output_)
        return 0;
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(output_), buffer,
                  static_cast<DWORD>(std::min<size_t>(size, MAXDWORD)), &got, nullptr))
        return GetLastError() == ERROR_BROKEN_PIPE ? 0 : -1;
    return static_cast<long>(got);
}

bool ChildProcess::waitOutput(int timeoutMs)
{
    if (!output_)
        return true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(static_cast<HANDLE>(output_), nullptr, 0, nullptr, &available, nullptr))
            return true;                                  // канал закрыт: EOF
        if (available > 0)
            return true;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        Sleep(20);
    }
}

void ChildProcess::closeOutput()
{
    if (output_)
        CloseHandle(static_cast<HANDLE>(output_));
    output_ = nullptr;
}

void ChildProcess::terminate() { kill(); }

void ChildProcess::kill()
{
    if (process_ && !waited_)
        TerminateProcess(static_cast<HANDLE>(process_), 137);
}

int ChildProcess::wait()
{
    if (!process_)
        return -1;
    if (!waited_)
    {
        WaitForSingleObject(static_cast<HANDLE>(process_), INFINITE);
        DWORD code = 0;
        exitCode_ = GetExitCodeProcess(static_cast<HANDLE>(process_), &code)
                        ? static_cast<int>(code) : -1;
        waited_ = true;
    }
    return exitCode_;
}

#else  // POSIX

namespace
{
#ifdef __linux__
// ioprio_set(IOPRIO_WHO_PROCESS, 0, IOPRIO_CLASS_IDLE) без linux/ioprio.h
void setIdleIoPriority()
{
    constexpr int kWhoProcess = 1;
    constexpr int kClassIdle = 3;
    constexpr int kClassShift = 13;
    syscall(SYS_ioprio_set, kWhoProcess, 0, kClassIdle << kClassShift);
}
#endif
}

bool ChildProcess::start(const std::vector<std::string>& argv,
                         const std::vector<std::string>& extraEnv,
                         const std::string& workdir, bool lowPriority, std::string& error)
{
    if (argv.empty())
    {
        error = "empty command";
        return false;
    }
    int pipeFd[2];
    if (pipe(pipeFd) != 0)
    {
        error = std::string("pipe: ") + strerror(errno);
        return false;
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
        return false;
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
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
        if (lowPriority)
        {
            setpriority(PRIO_PROCESS, 0, 15);
#ifdef __linux__
            setIdleIoPriority();
#endif
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
    output_ = pipeFd[0];
    pid_ = pid;
    waited_ = false;
    exitCode_ = -1;
    return true;
}

long ChildProcess::readOutput(void* buffer, size_t size)
{
    if (output_ < 0)
        return 0;
    for (;;)
    {
        const ssize_t got = read(output_, buffer, size);
        if (got < 0 && errno == EINTR)
            continue;
        return static_cast<long>(got);
    }
}

bool ChildProcess::waitOutput(int timeoutMs)
{
    if (output_ < 0)
        return true;
    for (;;)
    {
        pollfd descriptor{output_, POLLIN, 0};
        const int ready = poll(&descriptor, 1, timeoutMs);
        if (ready < 0 && errno == EINTR)
            continue;
        return ready > 0;
    }
}

void ChildProcess::closeOutput()
{
    if (output_ >= 0)
        close(output_);
    output_ = -1;
}

void ChildProcess::terminate()
{
    if (started() && !waited_)
        ::kill(static_cast<pid_t>(pid_), SIGTERM);
}

void ChildProcess::kill()
{
    if (started() && !waited_)
        ::kill(static_cast<pid_t>(pid_), SIGKILL);
}

int ChildProcess::wait()
{
    if (!started())
        return -1;
    if (!waited_)
    {
        int status = 0;
        while (waitpid(static_cast<pid_t>(pid_), &status, 0) < 0 && errno == EINTR) {}
        waited_ = true;
        if (WIFEXITED(status))
            exitCode_ = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            exitCode_ = 128 + WTERMSIG(status);
    }
    return exitCode_;
}

#endif
}
