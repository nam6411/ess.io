#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include "MqttManager.h"
#include "NetManager.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

// GET /api/system/info 및 <base>/sys/info 공용 (docs/13 §2, docs/07 §B.3)
namespace essio {

inline void sysInfoJson(JsonDocument& doc, NetManager& net, MqttManager& mqtt) {
    doc["device_id"] = deviceId();
    doc["fw"] = FW_VERSION;
    doc["build"] = __DATE__ " " __TIME__;
    doc["uptime_s"] = millis() / 1000;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["heap_min"] = ESP.getMinFreeHeap();
    doc["ip"] = net.staIp();
    doc["ap_ip"] = net.apIp();
    doc["rssi"] = net.rssi();
    doc["hostname"] = net.hostname();
    doc["wifi_state"] = net.stateName();
    doc["mqtt_state"] = mqtt.stateName();
    doc["mqtt_rc"] = mqtt.lastRc();
    doc["mqtt_published"] = mqtt.publishCount();
    doc["mqtt_failed"] = mqtt.publishFailCount();
    doc["boot_reason"] = (int)esp_reset_reason();
}

}  // namespace essio
