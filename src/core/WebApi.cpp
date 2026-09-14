#include "WebApi.h"

#include <AsyncJson.h>
#include <LittleFS.h>

#include "../modules/ModuleRegistry.h"
#include "Logger.h"
#include "SysInfo.h"

namespace essio {

void WebApi::begin(ConfigStore& store, NetManager& net, MqttManager& mqtt, Scheduler& scheduler, HaDiscovery& discovery) {
    store_ = &store;
    net_ = &net;
    mqtt_ = &mqtt;
    scheduler_ = &scheduler;
    discovery_ = &discovery;
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

void WebApi::handleSystemInfo(AsyncWebServerRequest* req) {
    if (!authorized(req)) return;
    JsonDocument doc;
    sysInfoJson(doc, *net_, *mqtt_);
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
    if (action == PendingAction::ApplyConfig) cfg.set(pendingConfig_);
    pending_ = PendingAction::None;
    pendingConfig_.clear();
    xSemaphoreGive(mutex_);

    switch (action) {
        case PendingAction::ApplyConfig: {
            String error;
            uint16_t changed = 0;
            if (!store_->fromJson(cfg.as<JsonVariantConst>(), error, changed)) {
                LOG_E("web: config apply failed: %s", error.c_str());
                break;
            }
            store_->save();
            LOG_I("web: config applied, changed=0x%04X", changed);
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
