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
    } else if (cfg.device.role == DeviceRole::Display) {
        baseTopic_ = cfg.display.topicRoot + "/display-" + deviceId();
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

// 브로커 주소 찾기 (docs/15-roles.md §5): 브로커 AP 게이트웨이 → UDP 비콘 → mDNS →
// 마지막 성공 주소(같은 SSID일 때) → 수동 입력. 설계서 §11.6의 mDNS·마지막 성공·수동 순서에
// 즉시 판단 가능한 두 경로를 앞에 붙였다. mdns_name을 비우면 자동 탐색(게이트웨이·비콘·mDNS)을
// 모두 건너뛰고 수동 주소만 쓴다 — 라우터가 브로드캐스트·mDNS를 막을 때의 탈출구.
bool MqttManager::resolveBroker() {
    const auto& m = store_->get().mqtt;
    const auto& w = store_->get().wifi;
    port_ = m.port;

    if (m.mdnsName.length()) {
        // 브로커 SoftAP(2순위 망)에 붙어 있으면 게이트웨이가 곧 브로커다
        if (w.fallbackSsid.length() && WiFi.SSID() == w.fallbackSsid && WiFi.gatewayIP() != IPAddress((uint32_t)0)) {
            host_ = WiFi.gatewayIP().toString();
            addressSource_ = "gateway";
            return true;
        }

        // 최근 30초 안에 받은 비콘 (라우터가 멀티캐스트를 막아 mDNS가 안 되는 망에서도 동작)
        pollBeacon();
        if (beaconMs_ && millis() - beaconMs_ < 30000) {
            host_ = beaconHost_;
            port_ = beaconPort_;
            addressSource_ = "beacon";
            return true;
        }

        IPAddress ip = MDNS.queryHost(m.mdnsName.c_str(), 2000);
        if (ip != IPAddress((uint32_t)0)) {
            host_ = ip.toString();
            addressSource_ = "mdns";
            return true;
        }
        LOG_W("mqtt: broker '%s' not found (beacon, mDNS)", m.mdnsName.c_str());
    }

    // 마지막 성공 주소는 "<SSID>\n<주소>"로 저장한다. 다른 망에서 얻은 주소(예: 브로커 AP의
    // 192.168.4.1)를 라우터 망에서 계속 붙잡고 실패하던 문제 때문에 같은 SSID일 때만 쓴다.
    // 쓰기 모드로 연다: 읽기 전용은 네임스페이스가 아직 없으면 매번 오류 로그를 남긴다
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        String saved = prefs.getString(NVS_KEY_BROKER, "");
        prefs.end();
        int nl = saved.indexOf('\n');
        saved = nl > 0 && saved.substring(0, nl) == WiFi.SSID() ? saved.substring(nl + 1) : String();
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

// 비콘: "essio-broker <hostname> <port>". hostname이 mqtt.mdns_name과 같은 브로커만 받는다
void MqttManager::pollBeacon() {
    if (!beaconListening_) beaconListening_ = beaconRx_.begin(BROKER_BEACON_PORT);
    if (!beaconListening_) return;
    const String& want = store_->get().mqtt.mdnsName;
    while (int len = beaconRx_.parsePacket()) {
        char buf[64];
        int n = beaconRx_.read((uint8_t*)buf, min(len, (int)sizeof(buf) - 1));
        buf[n > 0 ? n : 0] = '\0';
        char host[33];
        unsigned port = 0;
        if (sscanf(buf, "essio-broker %32s %u", host, &port) != 2 || !port || want != host) continue;
        const String addr = beaconRx_.remoteIP().toString();
        if (addr != beaconHost_) LOG_I("mqtt: broker beacon from %s:%u", addr.c_str(), port);
        beaconHost_ = addr;
        beaconPort_ = port;
        beaconMs_ = millis();
        // 재시도 대기 중이면 기다리지 않고 바로 붙는다
        if (state_ == MqttState::Backoff) nextAttemptMs_ = millis();
    }
}

void MqttManager::rememberAddress(const String& addr) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) return;
    const String entry = WiFi.SSID() + "\n" + addr;
    if (prefs.getString(NVS_KEY_BROKER, "") != entry) prefs.putString(NVS_KEY_BROKER, entry);
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
    client_.setServer(host_.c_str(), port_);
    String will = baseTopic_ + "/status";
    const char* user = m.username.length() ? m.username.c_str() : nullptr;
    const char* pass = m.password.length() ? m.password.c_str() : nullptr;

    LOG_I("mqtt: connecting to %s:%u (%s) as %s", host_.c_str(), port_, addressSource_, clientId_.c_str());
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
        // PubSubClient::loop()는 한 번에 패킷 하나만 읽는다. 루프가 느린 역할(디스플레이 그리기)에서도
        // 밀리지 않게 쌓인 것을 한 번에 비운다(상한을 둬 다른 일을 굶기지 않는다).
        uint8_t n = 0;
        do client_.loop();
        while (client_.connected() && net_.available() && ++n < 20);
        return;
    }
    pollBeacon();
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
