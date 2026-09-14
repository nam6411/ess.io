#include "MqttManager.h"

#include "Logger.h"

namespace essio {

namespace {
constexpr uint32_t BACKOFF_MIN_MS = 1000;
constexpr uint32_t BACKOFF_MAX_MS = 30000;
}

void MqttManager::begin(ConfigStore& store, bool (*wifiReady)()) {
    store_ = &store;
    wifiReady_ = wifiReady;
    client_.setClient(net_);
    client_.setBufferSize(BUFFER_SIZE);
    client_.setCallback([this](char* t, uint8_t* p, unsigned int n) { onRawMessage(t, p, n); });
    applyConfig();
}

void MqttManager::applyConfig() {
    const auto& m = store_->get().mqtt;
    if (client_.connected()) {
        publish(baseTopic_ + "/status", "offline", true);
        client_.disconnect();
    }
    baseTopic_ = m.baseTopic.length() ? m.baseTopic : "essio/" + deviceId();
    clientId_ = "essio-" + deviceId() + m.clientIdSuffix;
    host_ = m.host;  // PubSubClient는 포인터만 보관하므로 수명 유지 필요
    client_.setServer(host_.c_str(), m.port);
    client_.setKeepAlive(m.keepaliveS);
    backoffMs_ = BACKOFF_MIN_MS;
    nextAttemptMs_ = 0;
    state_ = (m.enabled && m.host.length() > 0) ? MqttState::Idle : MqttState::Disabled;
    if (state_ == MqttState::Disabled) LOG_I("mqtt: disabled");
}

void MqttManager::connect() {
    const auto& m = store_->get().mqtt;
    String will = baseTopic_ + "/status";
    const char* user = m.username.length() ? m.username.c_str() : nullptr;
    const char* pass = m.password.length() ? m.password.c_str() : nullptr;

    LOG_I("mqtt: connecting to %s:%u as %s", m.host.c_str(), m.port, clientId_.c_str());
    state_ = MqttState::Connecting;
    bool ok = client_.connect(clientId_.c_str(), user, pass, will.c_str(), 1, true, "offline");
    lastRc_ = client_.state();
    if (!ok) {
        LOG_W("mqtt: connect failed rc=%d, retry in %lu ms", lastRc_, (unsigned long)backoffMs_);
        state_ = MqttState::Backoff;
        nextAttemptMs_ = millis() + backoffMs_;
        backoffMs_ = min(backoffMs_ * 2, BACKOFF_MAX_MS);
        return;
    }
    state_ = MqttState::Connected;
    connectedSinceMs_ = millis();
    backoffMs_ = BACKOFF_MIN_MS;
    LOG_I("mqtt: connected");
    publish(will, "online", true);
    for (const String& t : subscriptions_) client_.subscribe(t.c_str());
    if (onConnected_) onConnected_();
}

void MqttManager::tick() {
    if (state_ == MqttState::Disabled) return;
    if (!wifiReady_ || !wifiReady_()) return;

    if (client_.connected()) {
        client_.loop();
        return;
    }
    if (state_ == MqttState::Connected) {
        LOG_W("mqtt: connection lost rc=%d", client_.state());
        state_ = MqttState::Backoff;
        nextAttemptMs_ = millis() + backoffMs_;
        return;
    }
    if (state_ == MqttState::Idle || (state_ == MqttState::Backoff && (int32_t)(millis() - nextAttemptMs_) >= 0)) {
        connect();
    }
}

bool MqttManager::publish(const String& topic, const String& payload, bool retain) {
    if (!client_.connected()) {
        publishFailCount_++;
        return false;
    }
    bool ok = client_.publish(topic.c_str(), (const uint8_t*)payload.c_str(), payload.length(), retain);
    if (ok) publishCount_++;
    else {
        publishFailCount_++;
        LOG_W("mqtt: publish failed %s (%u bytes)", topic.c_str(), payload.length());
    }
    return ok;
}

void MqttManager::subscribe(const String& topic) {
    for (const String& t : subscriptions_) {
        if (t == topic) return;
    }
    subscriptions_.push_back(topic);
    if (client_.connected()) client_.subscribe(topic.c_str());
}

void MqttManager::clearSubscriptions() {
    if (client_.connected()) {
        for (const String& t : subscriptions_) client_.unsubscribe(t.c_str());
    }
    subscriptions_.clear();
}

void MqttManager::onRawMessage(char* topic, uint8_t* payload, unsigned int length) {
    String t(topic);
    String p;
    p.reserve(length);
    for (unsigned int i = 0; i < length; i++) p += (char)payload[i];
    LOG_D("mqtt: rx %s = %s", t.c_str(), p.c_str());
    if (onMessage_) onMessage_(t, p);
}

const char* MqttManager::stateName() const {
    switch (state_) {
        case MqttState::Idle: return "idle";
        case MqttState::Connecting: return "connecting";
        case MqttState::Connected: return "connected";
        case MqttState::Backoff: return "backoff";
        default: return "disabled";
    }
}

}  // namespace essio
