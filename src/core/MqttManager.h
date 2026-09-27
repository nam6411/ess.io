#pragma once
#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

#include <functional>
#include <vector>

#include "Config.h"

// MQTT 연결 상태 머신 + 발행/구독. docs/07-mqtt-homeassistant.md §B, docs/11-architecture.md §5.2
namespace essio {

enum class MqttState : uint8_t { Disabled, Idle, Connecting, Connected, Backoff };

class MqttManager {
public:
    static constexpr uint16_t BUFFER_SIZE = 2048;
    using MessageHandler = std::function<void(const String& topic, const String& payload)>;
    using ConnectedHandler = std::function<void()>;

    // active=false면 포인터만 잡아두고 절대 접속하지 않는다 (Broker 역할에서 웹 API가 안전하게 조회하도록)
    void begin(ConfigStore& store, bool (*wifiReady)(), bool active = true);
    void applyConfig();  // mqtt.* 변경 시: 재접속
    void tick();

    bool connected() { return client_.connected(); }
    MqttState state() const { return state_; }
    const char* stateName() const;
    int lastRc() const { return lastRc_; }
    uint32_t connectedSinceMs() const { return connectedSinceMs_; }

    String baseTopic() const { return baseTopic_; }
    String discoveryPrefix() const { return store_->get().mqtt.discoveryPrefix; }
    String clientId() const { return clientId_; }
    String brokerAddress() const { return host_; }
    // mdns | gateway | last_good | manual | none — 어느 경로로 주소를 얻었는지 (docs/13-web-api.md)
    const char* addressSource() const { return addressSource_; }

    bool publish(const String& topic, const String& payload, bool retain = false);
    // 구독 목록에 추가(재접속 시 자동 재구독). 이미 연결돼 있으면 즉시 구독
    void subscribe(const String& topic);
    void clearSubscriptions();

    void setMessageHandler(MessageHandler h) { onMessage_ = h; }
    void setConnectedHandler(ConnectedHandler h) { onConnected_ = h; }

    uint32_t publishCount() const { return publishCount_; }
    uint32_t publishFailCount() const { return publishFailCount_; }

private:
    void connect();
    void onRawMessage(char* topic, uint8_t* payload, unsigned int length);
    // mDNS → NVS에 저장된 마지막 성공 IP → 수동 입력 주소 (설계서 §11.6)
    bool resolveBroker();
    void rememberAddress(const String& addr);

    ConfigStore* store_ = nullptr;
    bool (*wifiReady_)() = nullptr;
    bool active_ = true;
    WiFiClient net_;
    PubSubClient client_;
    MqttState state_ = MqttState::Disabled;
    String baseTopic_;
    String clientId_;
    String host_;
    const char* addressSource_ = "none";
    std::vector<String> subscriptions_;
    MessageHandler onMessage_;
    ConnectedHandler onConnected_;
    uint32_t nextAttemptMs_ = 0;
    uint32_t backoffMs_ = 1000;
    uint32_t connectedSinceMs_ = 0;
    int lastRc_ = 0;
    uint32_t publishCount_ = 0;
    uint32_t publishFailCount_ = 0;
};

}  // namespace essio
