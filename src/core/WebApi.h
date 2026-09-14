#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <functional>

#include "Config.h"
#include "HaDiscovery.h"
#include "MqttManager.h"
#include "NetManager.h"
#include "Scheduler.h"

// REST API + 정적 UI. 엔드포인트: docs/13-web-api.md
// 핸들러는 AsyncTCP 태스크에서 실행되므로 읽기(스냅샷)만 직접 하고, 쓰기는 pending으로 넘겨 tick()에서 처리.
namespace essio {

enum class PendingAction : uint8_t { None, ApplyConfig, Restart, FactoryReset, Rediscover, LegacyCleanup };

class WebApi {
public:
    using ApplyHandler = std::function<void(uint16_t changedSections)>;

    void begin(ConfigStore& store, NetManager& net, MqttManager& mqtt, Scheduler& scheduler, HaDiscovery& discovery);
    void tick();  // 메인 루프: pending 처리
    void setApplyHandler(ApplyHandler h) { onApply_ = h; }

private:
    bool authorized(AsyncWebServerRequest* req);
    void sendJson(AsyncWebServerRequest* req, JsonDocument& doc, int code = 200);
    void sendError(AsyncWebServerRequest* req, int code, const char* error, const String& message);
    void setupRoutes();

    void handleSystemInfo(AsyncWebServerRequest* req);
    void handleSlots(AsyncWebServerRequest* req);
    void handleConfigGet(AsyncWebServerRequest* req);
    void handleConfigPut(AsyncWebServerRequest* req, JsonVariant& json);
    void handleSchema(AsyncWebServerRequest* req);
    void handleLog(AsyncWebServerRequest* req);
    void handleSwitch(AsyncWebServerRequest* req, JsonVariant& json);
    void queueAction(AsyncWebServerRequest* req, PendingAction action);

    AsyncWebServer server_{80};
    ConfigStore* store_ = nullptr;
    NetManager* net_ = nullptr;
    MqttManager* mqtt_ = nullptr;
    Scheduler* scheduler_ = nullptr;
    HaDiscovery* discovery_ = nullptr;
    ApplyHandler onApply_;

    SemaphoreHandle_t mutex_ = nullptr;
    PendingAction pending_ = PendingAction::None;
    JsonDocument pendingConfig_;
    uint32_t pendingAtMs_ = 0;
};

}  // namespace essio
