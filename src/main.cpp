// ess.io2 — 조립만 담당. 구조: docs/11-architecture.md
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "core/Config.h"
#include "core/HaDiscovery.h"
#include "core/IoManager.h"
#include "core/Logger.h"
#include "core/MqttManager.h"
#include "core/NetManager.h"
#include "core/Scheduler.h"
#include "core/SysInfo.h"
#include "core/WebApi.h"
#include "port/SerialPort.h"

using namespace essio;

namespace {

constexpr uint32_t WDT_TIMEOUT_S = 15;
constexpr uint32_t SYS_INFO_INTERVAL_MS = 60000;

ConfigStore configStore;
NetManager net;
MqttManager mqtt;
SerialPort ports[MAX_PORTS];
Scheduler scheduler;
HaDiscovery discovery;
IoManager io;
WebApi web;

uint32_t lastSysInfoMs = 0;

bool wifiReady() { return net.staConnected(); }

// MQTT 수신: <base>/<slug>/switch/<name>/set, <base>/sys/cmd, <prefix>/status (docs/07 §B.3)
void onMqttMessage(const String& topic, const String& payload) {
    String base = mqtt.baseTopic();

    if (topic == mqtt.discoveryPrefix() + "/status") {
        if (payload == "online") {
            LOG_I("HA birth received, republishing discovery");
            discovery.publishAll();
            scheduler.publishAll();
        }
        return;
    }
    if (topic == base + "/sys/cmd") {
        if (payload == "restart") { delay(200); ESP.restart(); }
        else if (payload == "rediscover") { discovery.publishAll(); scheduler.publishAll(); }
        else if (payload == "factory_reset") { configStore.remove(); delay(200); ESP.restart(); }
        return;
    }
    if (!topic.startsWith(base + "/") || !topic.endsWith("/set")) return;

    // <base>/<slug>/switch/<name>/set
    String rest = topic.substring(base.length() + 1);
    int p1 = rest.indexOf("/switch/");
    if (p1 < 0) return;
    String slug = rest.substring(0, p1);
    String name = rest.substring(p1 + 8, rest.length() - 4);
    for (uint8_t i = 0; i < scheduler.slotCount(); i++) {
        Slot* s = scheduler.slot(i);
        if (s->enabled && s->slug == slug) {
            scheduler.enqueueSwitch(i, name.c_str(), payload == "ON");
            return;
        }
    }
    LOG_W("mqtt: no slot for %s", topic.c_str());
}

void subscribeAll() {
    mqtt.clearSubscriptions();
    String base = mqtt.baseTopic();
    mqtt.subscribe(mqtt.discoveryPrefix() + "/status");
    mqtt.subscribe(base + "/sys/cmd");
    for (uint8_t i = 0; i < scheduler.slotCount(); i++) {
        Slot* s = scheduler.slot(i);
        if (s->enabled) mqtt.subscribe(base + "/" + s->slug + "/switch/+/set");
    }
}

void onMqttConnected() {
    discovery.publishAll();
    scheduler.publishAll();
}

void publishSysInfo() {
    if (!mqtt.connected()) return;
    JsonDocument doc;
    sysInfoJson(doc, net, mqtt);
    String payload;
    serializeJson(doc, payload);
    mqtt.publish(mqtt.baseTopic() + "/sys/info", payload);
}

// 설정 변경 적용 범위 (docs/12-config-schema.md §4)
void applyConfig(uint16_t changed) {
    logger.setLevel(Logger::parseLevel(configStore.get().device.logLevel));
    if (changed & (CFG_WIFI | CFG_DEVICE)) net.applyConfig();
    if (changed & CFG_PORTS) scheduler.applyPorts();
    else if (changed & CFG_SLOTS) scheduler.applySlots();
    if (changed & (CFG_MQTT | CFG_SLOTS | CFG_PORTS)) {
        mqtt.applyConfig();
        subscribeAll();
    }
    if (changed & (CFG_IO | CFG_SLOTS)) io.applyConfig();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    logger.begin(LogLevel::Info);
    LOG_I("ess.io2 %s starting, device %s", FW_VERSION, deviceId().c_str());

    configStore.begin();
    logger.setLevel(Logger::parseLevel(configStore.get().device.logLevel));

    net.begin(configStore);
    mqtt.begin(configStore, wifiReady);
    mqtt.setMessageHandler(onMqttMessage);
    mqtt.setConnectedHandler(onMqttConnected);

    scheduler.begin(configStore, ports, mqtt);
    discovery.begin(configStore, mqtt, scheduler);
    io.begin(configStore, scheduler);
    subscribeAll();

    web.begin(configStore, net, mqtt, scheduler, discovery);
    web.setApplyHandler(applyConfig);

    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(nullptr);
    LOG_I("setup done, heap %u", ESP.getFreeHeap());
}

void loop() {
    esp_task_wdt_reset();
    net.tick();
    mqtt.tick();
    io.tick();
    scheduler.tick();
    web.tick();

    uint32_t now = millis();
    if (now - lastSysInfoMs >= SYS_INFO_INTERVAL_MS) {
        lastSysInfoMs = now;
        publishSysInfo();
    }
    delay(1);
}
