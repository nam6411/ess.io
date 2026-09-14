#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace essio {

enum class LogLevel : uint8_t { Error = 0, Warn, Info, Debug };

// 시리얼 출력 + 웹 조회용 링버퍼 (docs/13-web-api.md §2 /api/system/log)
class Logger {
public:
    static constexpr size_t CAPACITY = 100;
    static constexpr size_t LINE_LEN = 120;

    void begin(LogLevel level);
    void setLevel(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }

    void log(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
    void toJson(JsonDocument& doc, uint32_t sinceSeq) const;

    static LogLevel parseLevel(const String& s);
    static const char* levelName(LogLevel level);

private:
    struct Line {
        uint32_t seq;
        uint32_t ms;
        LogLevel level;
        char msg[LINE_LEN];
    };
    Line lines_[CAPACITY];
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t nextSeq_ = 1;
    LogLevel level_ = LogLevel::Info;
    SemaphoreHandle_t mutex_ = nullptr;
};

extern Logger logger;

}  // namespace essio

#define LOG_E(...) essio::logger.log(essio::LogLevel::Error, __VA_ARGS__)
#define LOG_W(...) essio::logger.log(essio::LogLevel::Warn, __VA_ARGS__)
#define LOG_I(...) essio::logger.log(essio::LogLevel::Info, __VA_ARGS__)
#define LOG_D(...) essio::logger.log(essio::LogLevel::Debug, __VA_ARGS__)
