#pragma once
#include <Arduino.h>

#include "Config.h"
#include "MqttManager.h"
#include "Scheduler.h"

// Home Assistant MQTT Discovery 발행. docs/07-mqtt-homeassistant.md §B.4
namespace essio {

class HaDiscovery {
public:
    void begin(ConfigStore& store, MqttManager& mqtt, Scheduler& scheduler);

    void publishAll();               // 활성 슬롯 전체 config 발행 (retain)
    void removeSlot(uint8_t slot);   // TODO: 마지막 발행 토픽 목록을 NVS에 보관해 빈 페이로드 발행
    void legacyCleanup();            // 기존 ess.io 토픽 삭제 (docs/03 §6.5 등)

private:
    void deviceJson(JsonObject dev);
    String configTopic(const char* component, const Slot& s, const char* entity);
    void publishSensor(const Slot& s, const SensorDef& d);
    void publishSwitch(const Slot& s, const SwitchDef& d);

    ConfigStore* store_ = nullptr;
    MqttManager* mqtt_ = nullptr;
    Scheduler* scheduler_ = nullptr;
};

}  // namespace essio
