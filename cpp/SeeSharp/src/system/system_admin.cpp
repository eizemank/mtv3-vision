#include "system/system_admin.hpp"

#include <array>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <sys/sysinfo.h>
#include <unistd.h>

SystemAdmin::SystemAdmin(const nlohmann::json& config)
{
    const auto section = config.value("system_admin", nlohmann::json::object());
    token_ = section.value("token", "");
    fileRoot_ = section.value("file_root", "/opt/seesharp");
    terminalEnabled_ = section.value("terminal_enabled", false);
}

bool SystemAdmin::authorized(const std::string& request) const
{
    if (token_.empty())
        return false;
    const std::string header = "X-Admin-Token: " + token_ + "\r\n";
    return request.find(header) != std::string::npos;
}

std::string SystemAdmin::runCommand(const std::string& command)
{
    std::array<char, 512> buffer{};
    std::string output;
    FILE* pipe = popen((command + " 2>&1").c_str(), "r");
    if (!pipe)
        throw std::runtime_error("cannot start command");
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe))
    {
        output += buffer.data();
        if (output.size() > 256 * 1024)
            break;
    }
    const int status = pclose(pipe);
    if (status != 0)
        throw std::runtime_error(output.empty() ? "command failed" : output);
    return output;
}

std::string SystemAdmin::shellQuote(const std::string& value)
{
    std::string quoted = "'";
    for (char character : value)
    {
        if (character == '\'')
            quoted += "'\\''";
        else
            quoted += character;
    }
    return quoted + "'";
}

nlohmann::json SystemAdmin::status() const
{
    struct sysinfo info{};
    if (sysinfo(&info) != 0)
        throw std::runtime_error("sysinfo failed");
    double temperature = 0.0;
    std::ifstream thermal("/sys/class/thermal/thermal_zone0/temp");
    thermal >> temperature;
    temperature /= 1000.0;
    std::ifstream stat("/proc/stat");
    std::string cpuLine;
    std::getline(stat, cpuLine);
    std::istringstream cpu(cpuLine);
    std::string label;
    unsigned long long user, nice, system, idle, ioWait, irq, softIrq, steal;
    cpu >> label >> user >> nice >> system >> idle >> ioWait >> irq >> softIrq >> steal;
    const auto busy = user + nice + system + irq + softIrq + steal;
    const auto total = busy + idle + ioWait;
    static unsigned long long previousBusy = 0;
    static unsigned long long previousTotal = 0;
    const auto totalDelta = total - previousTotal;
    const double cpuUsage = totalDelta ? 100.0 * (busy - previousBusy) / totalDelta : 0.0;
    previousBusy = busy;
    previousTotal = total;
    const unsigned long long unit = info.mem_unit;
    return {{"temperature_c", temperature},
            {"cpu_usage_percent", cpuUsage},
            {"memory_used_bytes", (info.totalram - info.freeram) * unit},
            {"memory_total_bytes", info.totalram * unit},
            {"uptime_seconds", info.uptime},
            {"load", {info.loads[0] / 65536.0, info.loads[1] / 65536.0,
                      info.loads[2] / 65536.0}}};
}

nlohmann::json SystemAdmin::processes() const
{
    const std::string output = runCommand("ps -eo pid=,pcpu=,pmem=,stat=,comm= --sort=-pcpu | head -n 51");
    nlohmann::json rows = nlohmann::json::array();
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line))
    {
        std::istringstream row(line);
        int pid;
        double cpu, memory;
        std::string state, name;
        if (row >> pid >> cpu >> memory >> state >> name)
            rows.push_back({{"pid", pid}, {"cpu", cpu}, {"memory", memory},
                            {"state", state}, {"name", name}});
    }
    return rows;
}

nlohmann::json SystemAdmin::network() const
{
    return {{"connections", runCommand("nmcli -t -f NAME,TYPE,DEVICE,STATE connection show")},
            {"addresses", runCommand("ip -j address show")},
            {"wifi_mode", runCommand("nmcli -t -f 802-11-wireless.mode connection show --active 2>/dev/null || true")}};
}

std::string SystemAdmin::resolvePath(const std::string& relativePath) const
{
    const auto root = std::filesystem::weakly_canonical(fileRoot_);
    const auto path = std::filesystem::weakly_canonical(root / relativePath);
    const auto rootText = root.string();
    const auto pathText = path.string();
    if (pathText != rootText && pathText.rfind(rootText + "/", 0) != 0)
        throw std::runtime_error("path is outside file_root");
    return pathText;
}

nlohmann::json SystemAdmin::listFiles(const std::string& relativePath) const
{
    nlohmann::json result = nlohmann::json::array();
    for (const auto& entry : std::filesystem::directory_iterator(resolvePath(relativePath)))
        result.push_back({{"name", entry.path().filename().string()},
                          {"directory", entry.is_directory()},
                          {"size", entry.is_regular_file() ? entry.file_size() : 0}});
    return result;
}

std::string SystemAdmin::readFile(const std::string& relativePath) const
{
    const std::string path = resolvePath(relativePath);
    if (std::filesystem::file_size(path) > 1024 * 1024)
        throw std::runtime_error("file exceeds 1 MiB");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("cannot open file");
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void SystemAdmin::writeFile(const std::string& relativePath, const std::string& content) const
{
    if (content.size() > 1024 * 1024)
        throw std::runtime_error("file exceeds 1 MiB");
    std::ofstream output(resolvePath(relativePath), std::ios::binary | std::ios::trunc);
    if (!output || !(output << content))
        throw std::runtime_error("cannot write file");
}

std::string SystemAdmin::processAction(int pid, const std::string& action) const
{
    const int signal = action == "stop" ? SIGTERM : action == "kill" ? SIGKILL :
                       action == "pause" ? SIGSTOP : action == "resume" ? SIGCONT : 0;
    if (pid <= 1 || signal == 0 || kill(pid, signal) != 0)
        throw std::runtime_error("process action failed");
    return "ok";
}

std::string SystemAdmin::configureNetwork(const nlohmann::json& request) const
{
    const std::string connection = request.at("connection").get<std::string>();
    const std::string mode = request.at("mode").get<std::string>();
    if (mode == "client")
    {
        const std::string ssid = request.at("ssid").get<std::string>();
        const std::string password = request.value("password", "");
        return runCommand("nmcli connection modify " + shellQuote(connection) +
                          " 802-11-wireless.mode infrastructure 802-11-wireless.ssid " +
                          shellQuote(ssid) + " wifi-sec.key-mgmt wpa-psk wifi-sec.psk " +
                          shellQuote(password) + " && nmcli connection up " + shellQuote(connection));
    }
    if (mode == "ap")
    {
        const std::string ssid = request.at("ssid").get<std::string>();
        const std::string password = request.at("password").get<std::string>();
        return runCommand("nmcli connection modify " + shellQuote(connection) +
                          " 802-11-wireless.mode ap 802-11-wireless.ssid " + shellQuote(ssid) +
                          " wifi-sec.key-mgmt wpa-psk wifi-sec.psk " + shellQuote(password) +
                          " ipv4.method shared && nmcli connection up " + shellQuote(connection));
    }
    if (mode == "static")
    {
        const std::string address = request.at("address").get<std::string>();
        const std::string gateway = request.at("gateway").get<std::string>();
        const std::string dns = request.value("dns", "");
        return runCommand("nmcli connection modify " + shellQuote(connection) +
                          " ipv4.method manual ipv4.addresses " + shellQuote(address) +
                          " ipv4.gateway " + shellQuote(gateway) + " ipv4.dns " + shellQuote(dns) +
                          " && nmcli connection up " + shellQuote(connection));
    }
    if (mode == "dhcp")
        return runCommand("nmcli connection modify " + shellQuote(connection) +
                          " ipv4.method auto && nmcli connection up " + shellQuote(connection));
    throw std::runtime_error("unsupported network mode");
}

std::string SystemAdmin::execute(const std::string& command) const
{
    if (!terminalEnabled_)
        throw std::runtime_error("terminal is disabled");
    return runCommand("timeout 10s /bin/sh -c " + shellQuote(command));
}
