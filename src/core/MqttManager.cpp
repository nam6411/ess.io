#include "MqttManager.h"

#include <ESPmDNS.h>
#include <Preferences.h>

#include "Logger.h"
#include "MqttMonitor.h"

namespace essio {

namespace {
constexpr uint32_t BACKOFF_MIN_MS = 1000;
constexpr uint32_t BACKOFF_MAX_MS = 30000;
constexpr const char* NVS_NAMESPACE = "essio";
constexpr const char* NVS_KEY_BROKER = "broker_ip";
}

void MqttManager::begin(ConfigStore& store, bool (*wifiReady)(), bool active) {
    store_ = &store;
    wifiReady_ = wifiReady;
    active_ = active;
    client_.setClient(net_);
    client_.setBufferSize(BUFFER_SIZE);
    client_.setCallback([this](char* t, uint8_t* p, unsigned int n) { onRawMessage(t, p, n); });
    applyConfig();
}

void MqttManager::applyConfig() {
    const Config& cfg = store_->get();
    const auto& m = cfg.mqtt;
    if (client_.connected()) {
        publish(baseTopic_ + "/status", "offline", true);
        client_.disconnect();
    }
    // 기본 base topic은 설계서 §3.4의 rv/<node> 형태를 따른다.
    if (m.baseTopic.length()) {
        baseTopic_ = m.baseTopic;
    } else if (cfg.device.role == DeviceRole::Broker) {
        baseTopic_ = "rv/broker";
    } else {
        int8_t slot = store_->firstEnabledSlot();
        baseTopic_ = "rv/" + (slot >= 0 ? cfg.slots[slot].slug : "node-" + deviceId());
    }
    clientId_ = "essio-" + deviceId() + m.clientIdSuffix;
    client_.setKeepAlive(m.keepaliveS);
    backoffMs_ = BACKOFF_MIN_MS;
    nextAttemptMs_ = 0;
    host_ = "";
    addressSource_ = "none";

    const bool haveAddress = m.host.length() > 0 || m.mdnsName.length() > 0;
    state_ = (active_ && m.enabled && haveAddress) ? MqttState::Idle : MqttState::Disabled;
    if (state_ == MqttState::Disabled && active_) LOG_I("mqtt: disabled (no broker address)");
}

// 설계서 §11.6: mDNS 기본 → 마지막 성공 IP → 수동 입력.
// mdns_name을 비우면 mDNS를 건너뛰므로 수동 주소만 쓰게 된다.
bool MqttManager::resolveBroker() {
    const auto& m = store_->get().mqtt;

    if (m.mdnsName.length()) {
        IPAddress ip = MDNS.queryHost(m.mdnsName.c_str(), 2000);
        if (ip != IPAddress((uint32_t)0)) {
            host_ = ip.toString();
            addressSource_ = "mdns";
            return true;
        }
        LOG_W("mqtt: mDNS '%s.local' not resolved", m.mdnsName.c_str());
    }

    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, true)) {
        String saved = prefs.getString(NVS_KEY_BROKER, "");
        prefs.end();
        if (saved.length()) {
            host_ = saved;
            addressSource_ = "last_good";
            LOG_I("mqtt: using last known broker %s", host_.c_str());
            return true;
        }
    }

    if (m.host.length()) {
        host_ = m.host;
        addressSource_ = "manual";
        return true;
    }

    addressSource_ = "none";
    return false;
}

void MqttManager::rememberAddress(const String& addr) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    if (prefs.getString(NVS_KEY_BROKER, "") != addr) prefs.putString(NVS_KEY_BROKER, addr);
    prefs.end();
}

void MqttManager::connect() {
    const auto& m = store_->get().mqtt;
    if (!resolveBroker()) {
        state_ = MqttState::Backoff;
        nextAttemptMs_ = millis() + backoffMs_;
        backoffMs_ = min(backoffMs_ * 2, BACKOFF_MAX_MS);
        return;
    }
    client_.setServer(host_.c_str(), m.port);
    String will = baseTopic_ + "/status";
    const char* user = m.username.length() ? m.username.c_str() : nullptr;
    const char* pass = m.password.length() ? m.password.c_str() : nullptr;

    LOG_I("mqtt: connecting to %s:%u (%s) as %s", host_.c_str(), m.port, addressSource_, clientId_.c_str());
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
    rememberAddress(host_);
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
    mqttMonitor.record(MqttDir::Tx, topic.c_str(), payload.c_str(), payload.length(), retain, ok);
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
    mqttMonitor.record(MqttDir::Rx, topic, p.c_str(), length, false);
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
