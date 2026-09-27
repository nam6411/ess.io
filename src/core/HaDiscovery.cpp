#include "HaDiscovery.h"

#include <WiFi.h>

#include "Logger.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace essio {

void HaDiscovery::begin(ConfigStore& store, MqttManager& mqtt, Scheduler& scheduler) {
    store_ = &store;
    mqtt_ = &mqtt;
    scheduler_ = &scheduler;
}

void HaDiscovery::deviceJson(JsonObject dev) {
    dev["ids"].to<JsonArray>().add(deviceId());
    JsonArray cns = dev["cns"].to<JsonArray>().add<JsonArray>();
    cns.add("mac");
    cns.add(WiFi.macAddress());
    dev["name"] = store_->get().device.name;
    dev["mf"] = "nam6411";
    dev["mdl"] = "ess.io2";
    dev["sw"] = FW_VERSION;
    if (WiFi.status() == WL_CONNECTED) dev["cu"] = "http://" + WiFi.localIP().toString() + "/";
}

String HaDiscovery::configTopic(const char* component, const Slot& s, const char* entity) {
    return mqtt_->discoveryPrefix() + "/" + component + "/" + deviceId() + "_" + s.slug + "/" + entity + "/config";
}

void HaDiscovery::publishSensor(const Slot& s, const SensorDef& d) {
    JsonDocument doc;
    String base = mqtt_->baseTopic();
    String prefix = scheduler_->slotPrefix(s);
    doc["uniq_id"] = deviceId() + "_" + s.slug + "_" + d.key;
    doc["obj_id"] = s.slug + "_" + d.key;
    doc["name"] = d.label;
    doc["stat_t"] = prefix + "/state";
    doc["val_tpl"] = String("{{ value_json.") + d.jsonPath + " }}";
    if (d.unit[0]) doc["unit_of_meas"] = d.unit;
    if (d.devClass[0]) doc["dev_cla"] = d.devClass;
    if (d.stateClass[0]) doc["stat_cla"] = d.stateClass;
    JsonArray avty = doc["avty"].to<JsonArray>();
    avty.add<JsonObject>()["t"] = base + "/status";
    avty.add<JsonObject>()["t"] = prefix + "/availability";
    doc["avty_mode"] = "all";
    deviceJson(doc["dev"].to<JsonObject>());
    String payload;
    serializeJson(doc, payload);
    mqtt_->publish(configTopic("sensor", s, d.key), payload, true);
}

void HaDiscovery::publishSwitch(const Slot& s, const SwitchDef& d) {
    JsonDocument doc;
    String base = mqtt_->baseTopic();
    String prefix = scheduler_->slotPrefix(s);
    doc["uniq_id"] = deviceId() + "_" + s.slug + "_" + d.name;
    doc["obj_id"] = s.slug + "_" + d.name;
    doc["name"] = d.label;
    doc["stat_t"] = prefix + "/switch/" + d.name + "/state";
    doc["cmd_t"] = prefix + "/switch/" + d.name + "/set";
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";
    JsonArray avty = doc["avty"].to<JsonArray>();
    avty.add<JsonObject>()["t"] = base + "/status";
    avty.add<JsonObject>()["t"] = prefix + "/availability";
    doc["avty_mode"] = "all";
    deviceJson(doc["dev"].to<JsonObject>());
    String payload;
    serializeJson(doc, payload);
    mqtt_->publish(configTopic("switch", s, d.name), payload, true);
}

void HaDiscovery::publishAll() {
    if (!store_->get().mqtt.discoveryEnabled || !mqtt_->connected()) return;
    uint16_t n = 0;
    for (uint8_t i = 0; i < scheduler_->slotCount(); i++) {
        const Slot& s = *scheduler_->slot(i);
        if (!s.enabled || !s.module) continue;
        for (size_t k = 0; k < s.module->sensorCount(); k++) {
            publishSensor(s, *s.module->sensorDef(k));
            n++;
        }
        for (size_t k = 0; k < s.module->switchCount(); k++) {
            publishSwitch(s, *s.module->switchDef(k));
            n++;
        }
    }
    LOG_I("discovery: published %u entities", n);
}

void HaDiscovery::removeSlot(uint8_t slot) {
    // TODO: docs/11-architecture.md §7 — 마지막 발행 토픽 목록 보관 후 빈 페이로드(retain) 발행
    (void)slot;
}

void HaDiscovery::legacyCleanup() {
    // TODO: docs/03-device-upower.md §6.5, docs/04 §6.4, docs/05 §4, docs/06 §4 토픽에 빈 페이로드 발행
    LOG_W("discovery: legacy cleanup not implemented");
}

}  // namespace essio
