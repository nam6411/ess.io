#pragma once
#include <Arduino.h>

#include <vector>

#include "Config.h"
#include "MqttManager.h"
#include "Scheduler.h"

// Home Assistant MQTT Discovery 발행. docs/07-mqtt-homeassistant.md §B.4
namespace essio {

class HaDiscovery {
public:
    void begin(ConfigStore& store, MqttManager& mqtt, Scheduler& scheduler);

    void publishAll();               // 현재 설정과 retained config를 동기화
    void removeAll();                // 이전에 발행한 config를 빈 retained payload로 삭제
    void removeSlot(uint8_t slot);   // 단일 장치 구성에서는 removeAll과 동일
    void legacyCleanup();            // 기존 ess.io 토픽 삭제 (docs/03 §6.5 등)

private:
    static constexpr const char* TOPICS_PATH = "/ha-discovery.json";
    static constexpr const char* TOPICS_TMP_PATH = "/ha-discovery.tmp";

    void deviceJson(JsonObject dev, const Slot& slot);
    String configTopic(const char* component, const Slot& s, const char* entity);
    bool publishSensor(const Slot& s, const SensorDef& d);
    bool publishSwitch(const Slot& s, const SwitchDef& d);
    bool loadTopics(std::vector<String>& topics);
    bool saveTopics(const std::vector<String>& topics);
    static bool contains(const std::vector<String>& topics, const String& topic);

    ConfigStore* store_ = nullptr;
    MqttManager* mqtt_ = nullptr;
    Scheduler* scheduler_ = nullptr;
};

}  // namespace essio
