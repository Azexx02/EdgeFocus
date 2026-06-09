#include "Logger.h"
#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <mutex>

namespace edgefocus {

LogLevel Logger::s_level = LogLevel::INFO;
static std::mutex g_log_mutex;

void Logger::setLevel(LogLevel level) { s_level = level; }
LogLevel Logger::level() { return s_level; }

static const char* levelStr(LogLevel lv) {
    switch (lv) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
    }
    return "?????";
}

void Logger::log(LogLevel lv, const char* tag, const char* fmt, va_list args) {
    if (lv < s_level) return;

    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()).count() % 1000;

    struct tm tm_buf {};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::lock_guard<std::mutex> lock(g_log_mutex);
    char timebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_buf);

    fprintf(stderr, "[%s.%03ld] [%s] [%s] ", timebuf, (long)ms, levelStr(lv), tag);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
}

void Logger::debug(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::DEBUG, tag, fmt, args);
    va_end(args);
}

void Logger::info(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::INFO, tag, fmt, args);
    va_end(args);
}

void Logger::warn(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::WARN, tag, fmt, args);
    va_end(args);
}

void Logger::error(const char* tag, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::ERROR, tag, fmt, args);
    va_end(args);
}

} // namespace edgefocus
