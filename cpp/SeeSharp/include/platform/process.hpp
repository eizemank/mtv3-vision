#pragma once
// Дочерний процесс с перехватом stdout+stderr: fork/execve (POSIX) или
// CreateProcess + Job Object (Windows). Используется тренером (TrainerJob).
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform
{
class ChildProcess
{
public:
    ChildProcess() = default;
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    /// argv[0] ищется в PATH; extraEnv — строки KEY=VALUE поверх окружения
    /// родителя; lowPriority — nice/ioprio idle (POSIX) или BELOW_NORMAL
    /// (Windows). Потомок умирает вместе с родителем (PR_SET_PDEATHSIG /
    /// Job Object KILL_ON_JOB_CLOSE).
    bool start(const std::vector<std::string>& argv,
               const std::vector<std::string>& extraEnv,
               const std::string& workdir, bool lowPriority, std::string& error);
    bool started() const;
    long long pid() const { return pid_; }

    /// Блокирующее чтение вывода: >0 байт, 0 — EOF, -1 — ошибка.
    long readOutput(void* buffer, size_t size);
    /// true — readOutput не заблокируется (есть данные или EOF); false — таймаут.
    bool waitOutput(int timeoutMs);
    void closeOutput();

    /// Мягкая остановка (SIGTERM); на Windows эквивалентна kill().
    void terminate();
    void kill();
    /// Ожидание завершения; код выхода (128+сигнал, если убит).
    int wait();

private:
    long long pid_ = -1;
#ifdef _WIN32
    void* process_ = nullptr;   // HANDLE процесса
    void* job_ = nullptr;       // HANDLE job object
    void* output_ = nullptr;    // HANDLE читающего конца канала
#else
    int output_ = -1;
#endif
    bool waited_ = false;
    int exitCode_ = -1;
};
}
