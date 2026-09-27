#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PicoMQTT.h>

#include <map>

#include "Config.h"

// Broker 역할: 보드 내장 로컬 MQTT 호스트. docs/15-roles.md
//
// PicoMQTT 서버는 retain 플래그를 그대로 전달하기만 하고 retained 메시지를 보관하지 않는다.
// 설계서 §3.4가 state/status를 retain=true로 두고 나중에 접속한 구독자(디스플레이·HA)가
// 현재 값을 즉시 받는 것을 전제하므로, 보관·재생을 여기서 구현한다.
namespace essio {

class EssioBroker : public PicoMQTT::Server {
public:
    EssioBroker(uint16_t port, const String& user, const String& pass, uint8_t maxClients, uint16_t retainSlots)
        : PicoMQTT::Server(port), user_(user), pass_(pass), maxClients_(maxClients), retainSlots_(retainSlots) {}

    size_t clientCount() const { return clients.size(); }
    size_t retainedCount() const { return retained_.size(); }
    uint32_t messages() const { return messages_; }
    uint32_t replays() const { return replays_; }
    uint32_t rejected() const { return rejected_; }
    uint32_t dropped() const { return dropped_; }

    static constexpr size_t MAX_RETAINED_PAYLOAD = 512;

    void countMessage() { messages_++; }
    void captureRetained(const char* topic, const char* payload, size_t len);
    void replayRetained(const char* filter);
    void clearRetained() { retained_.clear(); }

protected:
    PicoMQTT::ConnectReturnCode auth(const char* client_id, const char* username, const char* password) override;
    void on_connected(const char* client_id) override;
    void on_disconnected(const char* client_id) override;
    void on_subscribe(const char* client_id, const char* topic) override;

private:
    String user_, pass_;
    uint8_t maxClients_;
    uint16_t retainSlots_;
    std::map<String, String> retained_;
    uint32_t messages_ = 0, replays_ = 0, rejected_ = 0, dropped_ = 0;
};

class BrokerService {
public:
    void begin(ConfigStore& store);
    void end();
    void tick();
    bool active() const { return broker_ != nullptr; }

    bool publish(const String& topic, const String& payload, bool retain = false);
    void statusJson(JsonObject out) const;

    // 브로커 자신의 상태·진단 발행 (<base>/status, <base>/diag/*)
    void publishOwnStatus(const String& baseTopic, bool online);
    void publishOwnDiag(const String& baseTopic, const String& payload);

private:
    ConfigStore* store_ = nullptr;
    EssioBroker* broker_ = nullptr;
    uint16_t port_ = 1883;
};

}  // namespace essio
