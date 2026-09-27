#include "WebApi.h"

#include <AsyncJson.h>
#include <LittleFS.h>

#include "../modules/ModuleRegistry.h"
#include "Logger.h"
#include "MqttMonitor.h"
#include "SysInfo.h"

namespace essio {

void WebApi::begin(ConfigStore& store, NetManager& net, MqttManager& mqtt, Scheduler& scheduler, HaDiscovery& discovery,
                   BrokerService& broker) {
    store_ = &store;
    net_ = &net;
    mqtt_ = &mqtt;
    scheduler_ = &scheduler;
    discovery_ = &discovery;
    broker_ = &broker;
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    setupRoutes();
    server_.begin();
    LOG_I("web: started on :80");
}

bool WebApi::authorized(AsyncWebServerRequest* req) {
    const auto& a = store_->get().webAuth;
    if (!a.enabled) return true;
    if (req->authenticate(a.username.c_str(), a.password.c_str())) return true;
    req->requestAuthentication();
    return false;
}

void WebApi::sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code) {
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    res->setCode(code);
    serializeJson(doc, *res);
    req->send(res);
}

void WebApi::sendError(AsyncWebServerRequest* req, int code, const char* error, const String& message) {
    JsonDocument doc;
    doc["error"] = error;
    doc["message"] = message;
    sendJson(req, doc, code);
}

void WebApi::setupRoutes() {
    // 캡티브 포털 감지 경로 → AP 모드에서 루트로 (docs/13 §1)
    const char* captive[] = {"/generate_204", "/hotspot-detect.html", "/connecttest.txt", "/ncsi.txt", "/redirect"};
    for (const char* path : captive) {
        server_.on(path, HTTP_GET, [this](AsyncWebServerRequest* req) {
            if (net_->apActive()) req->redirect("http://" + net_->apIp() + "/");
            else req->send(204);
        });
    }

    server_.on("/api/system/info", HTTP_GET, [this](AsyncWebServerRequest* req) { handleSystemInfo(req); });
    server_.on("/api/system/log", HTTP_GET, [this](AsyncWebServerRequest* req) { handleLog(req); });
    server_.on("/api/mqtt", HTTP_GET, [this](AsyncWebServerRequest* req) { handleMqtt(req); });
    server_.on("/api/system/restart", HTTP_POST, [this](AsyncWebServerRequest* req) { queueAction(req, PendingAction::Restart); });
    server_.on("/api/system/rediscover", HTTP_POST, [this](AsyncWebServerRequest* req) { queueAction(req, PendingAction::Rediscover); });
    server_.on("/api/system/legacy_cleanup", HTTP_POST, [this](AsyncWebServerRequest* req) { queueAction(req, PendingAction::LegacyCleanup); });

    auto* factoryReset = new AsyncCallbackJsonWebHandler("/api/system/factory_reset", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        if (!authorized(req)) return;
        if (String(json["confirm"] | "") != "RESET") return sendError(req, 400, "bad_request", "confirm must be \"RESET\"");
        queueAction(req, PendingAction::FactoryReset);
    });
    factoryReset->setMethod(HTTP_POST);
    server_.addHandler(factoryReset);

    // 동작 모드: 브로커 호스트 / 장치 드라이버 (docs/15-roles.md)
    server_.on("/api/system/modes", HTTP_GET, [this](AsyncWebServerRequest* req) { handleModes(req); });
    auto* modeSet = new AsyncCallbackJsonWebHandler("/api/system/mode", [this](AsyncWebServerRequest* req, JsonVariant& json) {
        handleModeSet(req, json);
    });
    modeSet->setMethod(HTTP_POST);
    server_.addHandler(modeSet);

    server_.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* req) { handleConfigGet(req); });
    server_.on("/api/config/schema", HTTP_GET, [this](AsyncWebServerRequest* req) { handleSchema(req); });
    auto* configPut = new AsyncCallbackJsonWebHandler("/api/config", [this](AsyncWebServerRequest* req, JsonVariant& json) { handleConfigPut(req, json); });
    configPut->setMethod(HTTP_PUT);
    server_.addHandler(configPut);

    server_.on("/api/slots", HTTP_GET, [this](AsyncWebServerRequest* req) { handleSlots(req); });
    // JSON 핸들러는 접두 매칭만 지원 → POST /api/slots/{i}/switch/{name}, /api/slots/{i}/poll 을 한 곳에서 분기.
    // 본문은 항상 JSON이어야 함 (poll은 {} 전송).
    auto* slotPost = new AsyncCallbackJsonWebHandler("/api/slots", [this](AsyncWebServerRequest* req, JsonVariant& json) { handleSwitch(req, json); });
    slotPost->setMethod(HTTP_POST);
    server_.addHandler(slotPost);

    // TODO: /api/config/export, /api/config/import, /api/system/scan, /api/ports/{id}/modbus (docs/13 §2, §3, §5)

    server_.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("max-age=86400");
    server_.onNotFound([this](AsyncWebServerRequest* req) {
        if (net_->apActive() && req->method() == HTTP_GET && !req->url().startsWith("/api/")) {
            req->redirect("http://" + net_->apIp() + "/");
            return;
        }
        sendError(req, 404, "not_found", req->url());
    });
}

String WebApi::currentMode() const {
    const Config& cfg = store_->get();
    if (cfg.device.role == DeviceRole::Broker) return "broker";
    if (cfg.device.role == DeviceRole::Display) return "display";
    int8_t slot = store_->firstEnabledSlot();
    return slot >= 0 ? cfg.slots[slot].type : String("idle");
}

void WebApi::handleModes(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    JsonDocument doc;
    doc["current"] = currentMode();
    JsonArray modes = doc["modes"].to<JsonArray>();

    JsonObject b = modes.add<JsonObject>();
    b["id"] = "broker";
    b["label"] = "MQTT 호스트 (브로커)";
    b["role"] = "broker";
    b["port"] = store_->get().broker.port;

    size_t count = 0;
    const ModuleTypeInfo* types = ModuleRegistry::types(count);
    for (size_t i = 0; i < count; i++) {
        JsonObject o = modes.add<JsonObject>();
        o["id"] = types[i].type;
        o["label"] = types[i].label;
        o["role"] = "node";
        o["slug"] = types[i].slug;
        o["slave_id"] = types[i].slaveId;
        o["baud"] = types[i].baud;
    }

    JsonObject disp = modes.add<JsonObject>();
    disp["id"] = "display";
    disp["label"] = "디스플레이 (터치 화면)";
    disp["role"] = "display";
    disp["supported"] = ESSIO_DISPLAY ? true : false;  // 패널 드라이버가 이 빌드에 들어 있나
    JsonArray panels = disp["panels"].to<JsonArray>();
    for (const char* id : DISPLAY_PANELS) panels.add(id);

    JsonObject idle = modes.add<JsonObject>();
    idle["id"] = "idle";
    idle["label"] = "유휴 (발행 안 함)";
    idle["role"] = "node";

    sendJson(req, doc);
}

// 모드 하나를 고르면 role·slot·port 설정을 한 번에 맞춘다.
// 역할이 바뀌면 재부팅이 필요하고(CFG_ROLE), 드라이버만 바뀌면 해당 슬롯만 재초기화된다.
// 최초 설정 마법사는 config(wifi·display 등 부분 설정)를 함께 보내고 restart:true로 마무리한다.
void WebApi::handleModeSet(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!authorized(req)) return;
    String mode = json["mode"] | "";
    if (!mode.length()) return sendError(req, 400, "bad_request", "\"mode\" required");

    const bool toBroker = mode == "broker";
    const bool toDisplay = mode == "display";
    const bool toIdle = mode == "idle";
    const bool noSlot = toBroker || toDisplay || toIdle;
    const ModuleTypeInfo* info = noSlot ? nullptr : ModuleRegistry::find(mode);
    if (!noSlot && !info) return sendError(req, 400, "bad_request", "unknown mode: " + mode);
    if (toDisplay && !ESSIO_DISPLAY) return sendError(req, 400, "unsupported", "this firmware build has no display support");
    JsonVariantConst extra = json["config"];
    if (!extra.isNull() && !extra.is<JsonObjectConst>()) return sendError(req, 400, "bad_request", "\"config\" must be an object");
    const bool restart = json["restart"] | false;

    JsonDocument patch;
    // 마법사가 보낸 부분 설정을 먼저 깔고, 모드가 정하는 항목(role·slots·ports·base_topic)으로 덮는다
    if (!extra.isNull()) patch.set(extra);
    patch["device"]["role"] = toBroker ? "broker" : toDisplay ? "display" : "node";
    JsonArray slots = patch["slots"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        JsonObject s = slots.add<JsonObject>();
        s["index"] = i;
        if (i != 0 || noSlot) {
            s["enabled"] = false;
            if (i == 0 && noSlot) s["type"] = "none";
            continue;
        }
        s["enabled"] = true;
        s["type"] = info->type;
        s["slug"] = info->slug;
        s["label"] = info->label;
        s["port"] = 0;
        s["slave_id"] = info->slaveId;
        s["poll_interval_ms"] = info->pollMs;
    }
    if (info) {
        // 드라이버의 기본 보레이트를 포트 0에 반영 (docs/12 §2 ports[])
        JsonObject p = patch["ports"].to<JsonArray>().add<JsonObject>();
        p["id"] = 0;
        p["kind"] = "hw1";
        p["baud"] = info->baud;
    }
    // base_topic을 비워 역할·드라이버에 맞는 기본값(rv/<slug>, rv/broker)이 다시 계산되게 한다
    patch["mqtt"]["base_topic"] = "";

    String error;
    if (!store_->validate(patch.as<JsonVariantConst>(), error)) return sendError(req, 400, "validation", error);
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return sendError(req, 503, "busy", "try again");
    if (pending_ != PendingAction::None) {
        xSemaphoreGive(mutex_);
        return sendError(req, 409, "pending", "another action is pending");
    }
    pendingConfig_.set(patch);
    pending_ = restart ? PendingAction::ApplyConfigRestart : PendingAction::ApplyConfig;
    pendingAtMs_ = millis();
    xSemaphoreGive(mutex_);

    const bool roleChanges = String(patch["device"]["role"].as<const char*>()) != ConfigStore::roleName(store_->role());
    JsonDocument doc;
    doc["ok"] = true;
    doc["mode"] = mode;
    doc["restart_required"] = roleChanges || restart;
    sendJson(req, doc, 202);
}

void WebApi::handleSystemInfo(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    JsonDocument doc;
    sysInfoJson(doc, *net_, *mqtt_);
    doc["role"] = ConfigStore::roleName(store_->role());
    doc["mode"] = currentMode();
    doc["configured"] = store_->loadedFromFile();  // false면 웹 UI가 최초 설정 마법사를 띄운다
    if (store_->isBroker()) broker_->statusJson(doc["broker"].to<JsonObject>());
    JsonArray slots = doc["slots"].to<JsonArray>();
    for (uint8_t i = 0; i < scheduler_->slotCount(); i++) {
        const Slot* s = scheduler_->slot(i);
        JsonObject o = slots.add<JsonObject>();
        o["index"] = s->index;
        o["type"] = s->type;
        o["slug"] = s->slug;
        o["enabled"] = s->enabled;
        o["online"] = s->online;
        o["errors"] = s->errors;
    }
    sendJson(req, doc);
}

void WebApi::handleLog(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    uint32_t since = req->hasParam("since") ? req->getParam("since")->value().toInt() : 0;
    JsonDocument doc;
    logger.toJson(doc, since);
    sendJson(req, doc);
}

// 웹 UI 실시간 MQTT 뷰: 연결 상태 + since 이후 메시지 (docs/13-web-api.md)
void WebApi::handleMqtt(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    uint32_t since = req->hasParam("since") ? req->getParam("since")->value().toInt() : 0;
    JsonDocument doc;
    doc["role"] = ConfigStore::roleName(store_->role());
    if (store_->isBroker()) {
        JsonObject b = doc["broker"].to<JsonObject>();
        broker_->statusJson(b);
        mqttMonitor.clientsJson(b["client_list"].to<JsonArray>());
    } else {
        const auto& m = store_->get().mqtt;
        JsonObject c = doc["client"].to<JsonObject>();
        c["state"] = mqtt_->stateName();
        c["broker"] = mqtt_->brokerAddress();
        c["port"] = m.port;
        c["address_source"] = mqtt_->addressSource();
        c["mdns_name"] = m.mdnsName;
        c["client_id"] = mqtt_->clientId();
        c["base_topic"] = mqtt_->baseTopic();
        c["last_rc"] = mqtt_->lastRc();
        if (mqtt_->state() == MqttState::Connected) c["connected_s"] = (millis() - mqtt_->connectedSinceMs()) / 1000;
        c["published"] = mqtt_->publishCount();
        c["failed"] = mqtt_->publishFailCount();
        c["received"] = mqttMonitor.received();
    }
    mqttMonitor.messagesJson(doc, since);
    sendJson(req, doc);
}

void WebApi::handleSlots(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    JsonDocument doc;
    scheduler_->snapshot(doc);
    sendJson(req, doc);
}

void WebApi::handleConfigGet(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    JsonDocument doc;
    store_->toJson(doc, true);
    sendJson(req, doc);
}

void WebApi::handleSchema(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    JsonDocument doc;
    ModuleRegistry::schemaJson(doc.to<JsonObject>());
    sendJson(req, doc);
}

void WebApi::handleConfigPut(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!authorized(req)) return;
    String error;
    if (!store_->validate(json, error)) return sendError(req, 400, "validation", error);
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return sendError(req, 503, "busy", "try again");
    if (pending_ != PendingAction::None) {
        xSemaphoreGive(mutex_);
        return sendError(req, 409, "pending", "another action is pending");
    }
    pendingConfig_.set(json);
    pending_ = PendingAction::ApplyConfig;
    pendingAtMs_ = millis();
    xSemaphoreGive(mutex_);
    JsonDocument doc;
    doc["ok"] = true;
    doc["queued"] = true;
    sendJson(req, doc, 202);
}

void WebApi::handleSwitch(AsyncWebServerRequest* req, JsonVariant& json) {
    if (!authorized(req)) return;
    // /api/slots/{i}/switch/{name} | /api/slots/{i}/poll
    String url = req->url();
    if (!url.startsWith("/api/slots/")) return sendError(req, 404, "not_found", url);
    int slot = url.substring(11).toInt();
    if (url.endsWith("/poll")) {
        if (slot < 0 || slot >= scheduler_->slotCount()) return sendError(req, 404, "not_found", "unknown slot");
        scheduler_->requestPoll(slot);
        JsonDocument doc;
        doc["queued"] = true;
        return sendJson(req, doc, 202);
    }
    int p = url.indexOf("/switch/");
    if (p < 0) return sendError(req, 400, "bad_request", "malformed url");
    String name = url.substring(p + 8);
    if (!json["on"].is<bool>()) return sendError(req, 400, "bad_request", "\"on\" (bool) required");
    bool cur;
    if (!scheduler_->switchState(slot, name.c_str(), cur)) return sendError(req, 404, "not_found", "unknown slot/switch");
    scheduler_->enqueueSwitch(slot, name.c_str(), json["on"].as<bool>());
    JsonDocument doc;
    doc["queued"] = true;
    sendJson(req, doc, 202);
}

void WebApi::queueAction(AsyncWebServerRequest* req, PendingAction action) {
    if (!authorized(req)) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return sendError(req, 503, "busy", "try again");
    if (pending_ != PendingAction::None) {
        xSemaphoreGive(mutex_);
        return sendError(req, 409, "pending", "another action is pending");
    }
    pending_ = action;
    pendingAtMs_ = millis();
    xSemaphoreGive(mutex_);
    JsonDocument doc;
    doc["ok"] = true;
    sendJson(req, doc, 202);
}

void WebApi::tick() {
    if (pending_ == PendingAction::None) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;
    PendingAction action = pending_;
    JsonDocument cfg;
    if (action == PendingAction::ApplyConfig || action == PendingAction::ApplyConfigRestart) cfg.set(pendingConfig_);
    pending_ = PendingAction::None;
    pendingConfig_.clear();
    xSemaphoreGive(mutex_);

    switch (action) {
        case PendingAction::ApplyConfig:
        case PendingAction::ApplyConfigRestart: {
            String error;
            uint16_t changed = 0;
            if (!store_->fromJson(cfg.as<JsonVariantConst>(), error, changed)) {
                LOG_E("web: config apply failed: %s", error.c_str());
                break;
            }
            store_->save();
            LOG_I("web: config applied, changed=0x%04X", changed);
            if (action == PendingAction::ApplyConfigRestart) {
                LOG_W("web: restart after setup");
                delay(300);
                ESP.restart();
            }
            if (onApply_) onApply_(changed);
            break;
        }
        case PendingAction::Restart:
            LOG_W("web: restart requested");
            delay(300);
            ESP.restart();
            break;
        case PendingAction::FactoryReset:
            LOG_W("web: factory reset");
            store_->remove();
            delay(300);
            ESP.restart();
            break;
        case PendingAction::Rediscover:
            discovery_->publishAll();
            scheduler_->publishAll();
            break;
        case PendingAction::LegacyCleanup:
            discovery_->legacyCleanup();
            break;
        default:
            break;
    }
}

}  // namespace essio
