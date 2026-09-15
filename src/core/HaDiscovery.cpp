#include "HaDiscovery.h"

#include <LittleFS.h>
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

void HaDiscovery::deviceJson(JsonObject dev, const Slot& slot) {
    dev["ids"].to<JsonArray>().add(deviceId());
    JsonArray cns = dev["cns"].to<JsonArray>().add<JsonArray>();
    cns.add("mac");
    cns.add(WiFi.macAddress());
    dev["name"] = store_->get().device.name;
    dev["mf"] = slot.module->manufacturer();
    dev["mdl"] = slot.module->model();
    dev["sw"] = FW_VERSION;
    if (WiFi.status() == WL_CONNECTED) dev["cu"] = "http://" + WiFi.localIP().toString() + "/";
}

String HaDiscovery::configTopic(const char* component, const Slot& s, const char* entity) {
    return mqtt_->discoveryPrefix() + "/" + component + "/" + deviceId() + "_" + s.slug + "/" + entity + "/config";
}

bool HaDiscovery::publishSensor(const Slot& s, const SensorDef& d) {
    JsonDocument doc;
    String base = mqtt_->baseTopic();
    doc["uniq_id"] = deviceId() + "_" + s.slug + "_" + d.key;
    doc["obj_id"] = s.slug + "_" + d.key;
    doc["name"] = d.label;
    doc["stat_t"] = base + "/" + s.slug + "/state";
    doc["val_tpl"] = String("{{ value_json.") + d.jsonPath + " }}";
    if (d.unit[0]) doc["unit_of_meas"] = d.unit;
    if (d.devClass[0]) doc["dev_cla"] = d.devClass;
    if (d.stateClass[0]) doc["stat_cla"] = d.stateClass;
    JsonArray avty = doc["avty"].to<JsonArray>();
    avty.add<JsonObject>()["t"] = base + "/status";
    avty.add<JsonObject>()["t"] = base + "/" + s.slug + "/availability";
    doc["avty_mode"] = "all";
    deviceJson(doc["dev"].to<JsonObject>(), s);
    String payload;
    serializeJson(doc, payload);
    return mqtt_->publish(configTopic("sensor", s, d.key), payload, true);
}

bool HaDiscovery::publishSwitch(const Slot& s, const SwitchDef& d) {
    JsonDocument doc;
    String base = mqtt_->baseTopic();
    doc["uniq_id"] = deviceId() + "_" + s.slug + "_" + d.name;
    doc["obj_id"] = s.slug + "_" + d.name;
    doc["name"] = d.label;
    doc["stat_t"] = base + "/" + s.slug + "/switch/" + d.name + "/state";
    doc["cmd_t"] = base + "/" + s.slug + "/switch/" + d.name + "/set";
    doc["pl_on"] = "ON";
    doc["pl_off"] = "OFF";
    JsonArray avty = doc["avty"].to<JsonArray>();
    avty.add<JsonObject>()["t"] = base + "/status";
    avty.add<JsonObject>()["t"] = base + "/" + s.slug + "/availability";
    doc["avty_mode"] = "all";
    deviceJson(doc["dev"].to<JsonObject>(), s);
    String payload;
    serializeJson(doc, payload);
    return mqtt_->publish(configTopic("switch", s, d.name), payload, true);
}

void HaDiscovery::publishAll() {
    if (!mqtt_->connected()) return;
    if (!store_->get().mqtt.discoveryEnabled) {
        removeAll();
        return;
    }

    std::vector<String> previous;
    loadTopics(previous);
    std::vector<String> current;
    for (uint8_t i = 0; i < scheduler_->slotCount(); i++) {
        const Slot& s = *scheduler_->slot(i);
        if (!s.enabled || !s.module) continue;
        for (size_t k = 0; k < s.module->sensorCount(); k++) {
            current.push_back(configTopic("sensor", s, s.module->sensorDef(k)->key));
        }
        for (size_t k = 0; k < s.module->switchCount(); k++) {
            current.push_back(configTopic("switch", s, s.module->switchDef(k)->name));
        }
    }

    bool ok = true;
    uint16_t removed = 0;
    for (const String& topic : previous) {
        if (!contains(current, topic)) {
            if (mqtt_->publish(topic, "", true)) removed++;
            else ok = false;
        }
    }

    uint16_t n = 0;
    for (uint8_t i = 0; i < scheduler_->slotCount(); i++) {
        const Slot& s = *scheduler_->slot(i);
        if (!s.enabled || !s.module) continue;
        for (size_t k = 0; k < s.module->sensorCount(); k++) {
            if (!publishSensor(s, *s.module->sensorDef(k))) ok = false;
            n++;
        }
        for (size_t k = 0; k < s.module->switchCount(); k++) {
            if (!publishSwitch(s, *s.module->switchDef(k))) ok = false;
            n++;
        }
    }
    if (ok && !saveTopics(current)) ok = false;
    LOG_I("discovery: published %u entities, removed %u stale%s", n, removed, ok ? "" : " (incomplete)");
}

void HaDiscovery::removeAll() {
    if (!mqtt_->connected()) return;
    std::vector<String> previous;
    if (!loadTopics(previous) || previous.empty()) return;
    std::vector<String> failed;
    for (const String& topic : previous) {
        if (!mqtt_->publish(topic, "", true)) failed.push_back(topic);
    }
    saveTopics(failed);
    LOG_I("discovery: removed %u, failed %u", (unsigned)(previous.size() - failed.size()), (unsigned)failed.size());
}

void HaDiscovery::removeSlot(uint8_t slot) {
    (void)slot;
    removeAll();
}

bool HaDiscovery::loadTopics(std::vector<String>& topics) {
    topics.clear();
    File f = LittleFS.open(TOPICS_PATH, "r");
    if (!f) return true;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, f);
    f.close();
    if (error || !doc.is<JsonArray>()) {
        LOG_W("discovery: invalid topic cache");
        return false;
    }
    for (JsonVariantConst value : doc.as<JsonArrayConst>()) {
        const char* topic = value.as<const char*>();
        if (topic && topic[0]) topics.emplace_back(topic);
    }
    return true;
}

bool HaDiscovery::saveTopics(const std::vector<String>& topics) {
    JsonDocument doc;
    JsonArray array = doc.to<JsonArray>();
    for (const String& topic : topics) array.add(topic);
    File f = LittleFS.open(TOPICS_TMP_PATH, "w");
    if (!f) return false;
    bool ok = serializeJson(doc, f) > 0;
    f.close();
    if (!ok) {
        LittleFS.remove(TOPICS_TMP_PATH);
        return false;
    }
    LittleFS.remove(TOPICS_PATH);
    if (!LittleFS.rename(TOPICS_TMP_PATH, TOPICS_PATH)) return false;
    return true;
}

bool HaDiscovery::contains(const std::vector<String>& topics, const String& topic) {
    for (const String& item : topics) {
        if (item == topic) return true;
    }
    return false;
}

void HaDiscovery::legacyCleanup() {
    // TODO: docs/03-device-upower.md §6.5, docs/04 §6.4, docs/05 §4, docs/06 §4 토픽에 빈 페이로드 발행
    LOG_W("discovery: legacy cleanup not implemented");
}

}  // namespace essio
