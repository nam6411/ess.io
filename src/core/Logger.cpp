#include "Logger.h"

#include <stdarg.h>

namespace essio {

Logger logger;

void Logger::begin(LogLevel level) {
    level_ = level;
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
}

void Logger::log(LogLevel level, const char* fmt, ...) {
    if (level > level_) return;

    char buf[LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    uint32_t now = millis();
    Serial.printf("[%8lu] %s %s\n", (unsigned long)now, levelName(level), buf);

    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        Line& line = lines_[head_];
        line.seq = nextSeq_++;
        line.ms = now;
        line.level = level;
        strlcpy(line.msg, buf, LINE_LEN);
        head_ = (head_ + 1) % CAPACITY;
        if (count_ < CAPACITY) count_++;
        xSemaphoreGive(mutex_);
    }
}

void Logger::toJson(JsonDocument& doc, uint32_t sinceSeq) const {
    JsonArray lines = doc["lines"].to<JsonArray>();
    uint32_t next = sinceSeq;
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t start = (head_ + CAPACITY - count_) % CAPACITY;
        for (size_t i = 0; i < count_; i++) {
            const Line& line = lines_[(start + i) % CAPACITY];
            if (line.seq <= sinceSeq) continue;
            JsonObject o = lines.add<JsonObject>();
            o["seq"] = line.seq;
            o["ms"] = line.ms;
            o["level"] = levelName(line.level);
            o["msg"] = line.msg;
            next = line.seq;
        }
        xSemaphoreGive(mutex_);
    }
    doc["next"] = next;
}

LogLevel Logger::parseLevel(const String& s) {
    if (s == "error") return LogLevel::Error;
    if (s == "warn") return LogLevel::Warn;
    if (s == "debug") return LogLevel::Debug;
    return LogLevel::Info;
}

const char* Logger::levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "error";
        case LogLevel::Warn: return "warn";
        case LogLevel::Info: return "info";
        default: return "debug";
    }
}

}  // namespace essio
