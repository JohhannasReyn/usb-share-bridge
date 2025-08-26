#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
public:
    static Logger& instance();
    
    void setLogLevel(LogLevel level) { m_logLevel = level; }
    void setLogFile(const std::string& filename);
    void enableConsoleOutput(bool enable) { m_consoleOutput = enable; }
    
    void log(LogLevel level, const std::string& message, const std::string& category = "");
    void debug(const std::string& message, const std::string& category = "");
    void info(const std::string& message, const std::string& category = "");
    void warning(const std::string& message, const std::string& category = "");
    void error(const std::string& message, const std::string& category = "");
    void fatal(const std::string& message, const std::string& category = "");
    
    void flush();

private:
    Logger() = default;
    std::string formatMessage(LogLevel level, const std::string& message, const std::string& category);
    std::string getCurrentTimestamp();
    std::string levelToString(LogLevel level);
    
    LogLevel m_logLevel = LogLevel::INFO;
    std::unique_ptr<std::ofstream> m_logFile;
    std::mutex m_mutex;
    bool m_consoleOutput = true;
};

// Convenience macros
#define LOG_DEBUG(msg, cat) Logger::instance().debug(msg, cat)
#define LOG_INFO(msg, cat) Logger::instance().info(msg, cat)
#define LOG_WARNING(msg, cat) Logger::instance().warning(msg, cat)
#define LOG_ERROR(msg, cat) Logger::instance().error(msg, cat)
#define LOG_FATAL(msg, cat) Logger::instance().fatal(msg, cat)
