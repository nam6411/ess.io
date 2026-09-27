// ess.io2 — 조립만 담당. 구조: docs/11-architecture.md, 역할: docs/15-roles.md
//
// 한 펌웨어가 세 가지로 동작한다. 웹 UI에서 고른 device.role이 어느 쪽인지 결정한다.
//   Broker  — 내장 MQTT 호스트. 장치 폴링·슬롯 없음.
//   Node    — 장치 1대를 폴링해 브로커로 발행하는 클라이언트.
//   Display — 브로커에 붙어 모든 노드를 화면에 보이고 터치로 스위치를 조작. 장치 폴링·슬롯 없음.
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "core/BrokerService.h"
#include "core/Config.h"
#include "core/HaDiscovery.h"
#include "core/IoManager.h"
#include "core/Logger.h"
#include "core/MqttManager.h"
#include "core/MqttMonitor.h"
#include "core/NetManager.h"
#include "core/Scheduler.h"
#include "core/SysInfo.h"
#include "core/WebApi.h"
#include "display/DisplayService.h"
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
BrokerService broker;
WebApi web;
DisplayService display;

bool brokerRole = false;
bool displayRole = false;
uint32_t lastSysInfoMs = 0;

bool wifiReady() { return net.staConnected(); }

// MQTT 수신 (Node 역할): <prefix>/switch/<name>/set, <base>/sys/cmd, <discovery>/status
void onMqttMessage(const String& topic, const String& payload) {
    String base = mqtt.baseTopic();
    if (displayRole && display.onMessage(topic, payload)) return;

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
    if (!topic.endsWith("/set")) return;

    for (uint8_t i = 0; i < scheduler.slotCount(); i++) {
        Slot* s = scheduler.slot(i);
        if (!s->enabled) continue;
        String p = scheduler.slotPrefix(*s) + "/switch/";
        if (!topic.startsWith(p)) continue;
        String name = topic.substring(p.length(), topic.length() - 4);
        scheduler.enqueueSwitch(i, name.c_str(), payload == "ON");
        return;
    }
    LOG_W("mqtt: no slot for %s", topic.c_str());
}

void subscribeAll() {
    mqtt.clearSubscriptions();
    String base = mqtt.baseTopic();
    mqtt.subscribe(mqtt.discoveryPrefix() + "/status");
    mqtt.subscribe(base + "/sys/cmd");
    if (displayRole) {
        display.subscribeAll();
        return;
    }
    for (uint8_t i = 0; i < scheduler.slotCount(); i++) {
        Slot* s = scheduler.slot(i);
        if (s->enabled) mqtt.subscribe(scheduler.slotPrefix(*s) + "/switch/+/set");
    }
}

void onMqttConnected() {
    mqtt.publish(mqtt.baseTopic() + "/status", "online", true);
    if (displayRole) return;
    discovery.publishAll();
    scheduler.publishAll();
}

void publishSysInfo() {
    JsonDocument doc;
    sysInfoJson(doc, net, mqtt);
    doc["role"] = ConfigStore::roleName(configStore.role());
    String payload;
    if (brokerRole) {
        broker.statusJson(doc["broker"].to<JsonObject>());
        serializeJson(doc, payload);
        broker.publishOwnDiag("rv/broker", payload);
        broker.publishOwnStatus("rv/broker", true);
        return;
    }
    if (!mqtt.connected()) return;
    serializeJson(doc, payload);
    mqtt.publish(mqtt.baseTopic() + "/sys/info", payload);
}

// 설정 변경 적용 범위 (docs/12-config-schema.md §4)
void applyConfig(uint16_t changed) {
    logger.setLevel(Logger::parseLevel(configStore.get().device.logLevel));

    if (changed & CFG_ROLE) {
        // 역할 전환은 브로커·스케줄러·포트 소유권이 통째로 바뀌므로 재부팅으로만 적용한다.
        LOG_W("role changed to '%s', restarting", ConfigStore::roleName(configStore.role()));
        delay(300);
        ESP.restart();
        return;
    }

    if (changed & (CFG_WIFI | CFG_DEVICE)) net.applyConfig();

    if (brokerRole) {
        if (changed & CFG_BROKER) broker.begin(configStore);
        return;
    }
    if (displayRole) {
        if (changed & CFG_DISPLAY) display.applyConfig();
        if (changed & CFG_MQTT) {
            mqtt.applyConfig();
            subscribeAll();
        }
        return;
    }

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
    mqttMonitor.begin();
    LOG_I("ess.io2 %s starting, device %s", FW_VERSION, deviceId().c_str());

    configStore.begin();
    logger.setLevel(Logger::parseLevel(configStore.get().device.logLevel));
    brokerRole = configStore.isBroker();
    displayRole = configStore.isDisplay();
    LOG_I("role: %s", ConfigStore::roleName(configStore.role()));

    net.begin(configStore);

    // 모든 역할에서 포인터는 연결해 둔다. Broker 역할에서는 MQTT 클라이언트를 active=false로 두어
    // 접속하지 않는다. 슬롯·포트는 Node에서만 만든다 — Display 보드는 GPIO 대부분을 패널이 쓴다.
    mqtt.begin(configStore, wifiReady, !brokerRole);
    scheduler.begin(configStore, ports, mqtt, !brokerRole && !displayRole);
    discovery.begin(configStore, mqtt, scheduler);

    if (brokerRole) {
        broker.begin(configStore);
    } else if (displayRole) {
        display.begin(configStore, mqtt, net);
        mqtt.setMessageHandler(onMqttMessage);
        mqtt.setConnectedHandler(onMqttConnected);
        subscribeAll();
    } else {
        mqtt.setMessageHandler(onMqttMessage);
        mqtt.setConnectedHandler(onMqttConnected);
        io.begin(configStore, scheduler);
        subscribeAll();
    }

    web.begin(configStore, net, mqtt, scheduler, discovery, broker);
    web.setApplyHandler(applyConfig);

    // core 3.x는 부팅 때 TWDT를 이미 초기화하므로 reconfigure로 제한 시간만 바꾼다.
    const esp_task_wdt_config_t wdt = {.timeout_ms = WDT_TIMEOUT_S * 1000, .idle_core_mask = 0, .trigger_panic = true};
    if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
    esp_task_wdt_add(nullptr);
    LOG_I("setup done, heap %u", (unsigned)ESP.getFreeHeap());
}

void loop() {
    esp_task_wdt_reset();
    net.tick();

    if (brokerRole) {
        broker.tick();
    } else if (displayRole) {
        mqtt.tick();
        display.tick();
    } else {
        mqtt.tick();
        io.tick();
        scheduler.tick();
    }

    web.tick();

    uint32_t now = millis();
    if (now - lastSysInfoMs >= SYS_INFO_INTERVAL_MS) {
        lastSysInfoMs = now;
        publishSysInfo();
    }
    delay(1);
}
