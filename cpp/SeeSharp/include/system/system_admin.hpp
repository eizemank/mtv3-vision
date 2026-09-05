#pragma once

#include <string>

#include <nlohmann/json.hpp>

class SystemAdmin
{
public:
    explicit SystemAdmin(const nlohmann::json& config);

    bool authorized(const std::string& request) const;
    bool permitsConfiguration(const nlohmann::json& config) const;
    nlohmann::json status() const;
    nlohmann::json processes() const;
    nlohmann::json network() const;
    nlohmann::json listFiles(const std::string& relativePath) const;
    std::string readFile(const std::string& relativePath) const;
    void writeFile(const std::string& relativePath, const std::string& content) const;
    std::string processAction(int pid, const std::string& action) const;
    std::string configureNetwork(const nlohmann::json& request) const;
    std::string execute(const std::string& command) const;
    bool terminalEnabled() const { return terminalEnabled_; }

private:
    std::string token_;
    std::string fileRoot_;
    std::string networkHelper_;
    bool terminalEnabled_ = false;

    std::string resolvePath(const std::string& relativePath) const;
    static std::string runCommand(const std::string& command);
    static std::string shellQuote(const std::string& value);
};
