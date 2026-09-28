#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include "../port/SerialPort.h"

// 장치 모듈 인터페이스. docs/11-architecture.md §3.1
namespace essio {

struct SwitchDef {
    const char* name;   // 토픽/명령 이름 (예: "inverter", "ch6")
    const char* label;  // HA 표시명
};

struct SensorDef {
    const char* key;         // 엔티티 키 (uniq_id 접미)
    const char* label;
    const char* unit;        // "" 허용
    const char* devClass;    // HA device_class, "" 허용
    const char* stateClass;  // measurement | total_increasing | ""
    const char* jsonPath;    // value_template 경로 (예: "pv.in_v")
};

enum class PollResult : uint8_t { Busy, Done, Error };

class IDeviceModule {
public:
    virtual ~IDeviceModule() {}

    virtual const char* type() const = 0;

    // 슬롯 활성화 시 1회. params는 slots[i].params
    virtual bool begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) = 0;
    virtual void end() {}

    // 호출마다 최대 1개 요청. 포트 lock은 스케줄러가 잡고 들어옴.
    virtual PollResult pollStep() = 0;
    virtual const char* lastError() const { return ""; }

    // 상태 → JSON (MQTT state 페이로드 = 웹 상태 API)
    virtual void toJson(JsonObject out) const = 0;

    virtual size_t switchCount() const { return 0; }
    virtual const SwitchDef* switchDef(size_t) const { return nullptr; }
    virtual bool switchState(size_t) const { return false; }
    virtual bool writeSwitch(size_t, bool) { return false; }

    virtual size_t sensorCount() const { return 0; }
    virtual const SensorDef* sensorDef(size_t) const { return nullptr; }
    // 디스플레이가 먼저 보여줄 센서 키 (쉼표 구분, 첫 번째가 대표값). <prefix>/meta로 발행된다.
    virtual const char* keySensors() const { return ""; }
    // 디스플레이 테두리 그래프 (meta.ring). "battery:<soc 경로>,<전력 경로>" = SOC + 충·방전량,
    // "flows:<경로>,<경로>,<경로>" = 전력 흐름 3색. 빈 문자열이면 그리지 않는다.
    virtual const char* displayRing() const { return ""; }

    int switchIndex(const char* name) const {
        for (size_t i = 0; i < switchCount(); i++) {
            if (strcmp(switchDef(i)->name, name) == 0) return (int)i;
        }
        return -1;
    }

protected:
    SerialPort* port_ = nullptr;
    uint8_t slaveId_ = 0;
};

}  // namespace essio
