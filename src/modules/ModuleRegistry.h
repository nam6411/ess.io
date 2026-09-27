#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace essio {

class IDeviceModule;

struct ModuleTypeInfo {
    const char* type;
    const char* label;
    uint8_t maxChannels;  // rtusw 계열만 의미 있음, 그 외 0
    const char* slug;     // 기본 토픽 이름 (rv/<slug>/...) — 설계서 §11.2
    uint8_t slaveId;      // 기본 슬레이브 주소
    uint32_t baud;        // 기본 보레이트
    uint32_t pollMs;      // 기본 폴링 주기
};

// type 문자열 → 모듈 팩토리. 새 모듈은 여기에만 등록하면 슬롯 선택 목록/기본 params에 나타난다.
class ModuleRegistry {
public:
    static const ModuleTypeInfo* types(size_t& count);
    static const ModuleTypeInfo* find(const String& type);
    static bool isKnown(const String& type);
    static IDeviceModule* create(const String& type);
    static void defaultParams(const String& type, JsonObject out);
    static void schemaJson(JsonObject out);  // GET /api/config/schema
};

}  // namespace essio
