#pragma once
#include <string>

namespace edgefocus {

enum class LogLevel {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3
};

/// Thread-safe logger. Outputs to stdout with timestamp and level prefix.
class Logger {
public:
    static void setLevel(LogLevel level);
    static LogLevel level();

    static void debug(const char* tag, const char* fmt, ...);
    static void info (const char* tag, const char* fmt, ...);
    static void warn (const char* tag, const char* fmt, ...);
    static void error(const char* tag, const char* fmt, ...);

private:
    static void log(LogLevel lv, const char* tag, const char* fmt, va_list args);
    static LogLevel s_level;
};

// Convenience macros
#define LOGD(tag, ...) ::edgefocus::Logger::debug(tag, __VA_ARGS__)
#define LOGI(tag, ...) ::edgefocus::Logger::info (tag, __VA_ARGS__)
#define LOGW(tag, ...) ::edgefocus::Logger::warn (tag, __VA_ARGS__)
#define LOGE(tag, ...) ::edgefocus::Logger::error(tag, __VA_ARGS__)

} // namespace edgefocus
