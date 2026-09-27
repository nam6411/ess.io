#include "BrokerService.h"

#include "Logger.h"
#include "MqttMonitor.h"

namespace essio {

namespace {
// 이 접두에 해당하는 토픽만 retain 보관 대상 (cmd는 보관하지 않는다 — 재접속 시 재실행 위험)
bool retainable(const char* topic) {
    return PicoMQTT::Subscriber::topic_matches("+/+/state/#", topic) ||
           PicoMQTT::Subscriber::topic_matches("+/+/state", topic) ||
           PicoMQTT::Subscriber::topic_matches("+/+/status", topic) ||
           PicoMQTT::Subscriber::topic_matches("+/+/availability", topic) ||
           PicoMQTT::Subscriber::topic_matches("+/+/switch/#", topic) ||
           PicoMQTT::Subscriber::topic_matches("+/status", topic) ||
           PicoMQTT::Subscriber::topic_matches("+/diag/#", topic) ||
           PicoMQTT::Subscriber::topic_matches("+/sys/info", topic) ||
           PicoMQTT::Subscriber::topic_matches("homeassistant/#", topic);
}
}  // namespace

PicoMQTT::ConnectReturnCode EssioBroker::auth(const char* client_id, const char* username, const char* password) {
    if (clients.size() > maxClients_) {
        LOG_W("broker: '%s' rejected — max_clients %u reached", client_id, maxClients_);
        rejected_++;
        return PicoMQTT::CRC_SERVER_UNAVAILABLE;
    }
    if (user_.length() > 0) {
        if (!username || !password || user_ != username || pass_ != password) {
            LOG_W("broker: '%s' rejected — bad credentials", client_id);
            rejected_++;
            return PicoMQTT::CRC_BAD_USERNAME_OR_PASSWORD;
        }
    }
    return PicoMQTT::CRC_ACCEPTED;
}

void EssioBroker::on_connected(const char* client_id) {
    LOG_I("broker: '%s' connected (%u clients)", client_id, (unsigned)clients.size());
    mqttMonitor.clientConnected(client_id);
}

void EssioBroker::on_disconnected(const char* client_id) {
    LOG_I("broker: '%s' disconnected (%u clients)", client_id, (unsigned)clients.size());
    mqttMonitor.clientDisconnected(client_id);
}

// 구독이 걸리면 그 패턴에 맞는 retained 값을 다시 흘려보낸다.
// PicoMQTT는 특정 클라이언트에만 보내는 경로를 노출하지 않으므로 버스 전체에 재발행한다.
// state류는 현재값이라 중복 수신이 무해하다(멱등).
void EssioBroker::on_subscribe(const char* client_id, const char* topic) {
    LOG_D("broker: '%s' subscribed %s", client_id, topic);
    replayRetained(topic);
}

void EssioBroker::captureRetained(const char* topic, const char* payload, size_t len) {
    if (!retainable(topic)) return;

    if (!payload || len > MAX_RETAINED_PAYLOAD) {  // 보관 한도 초과
        dropped_++;
        return;
    }
    if (len == 0) {  // 빈 페이로드 = 삭제 (HA discovery 정리 규약)
        retained_.erase(String(topic));
        return;
    }
    String key(topic);
    if (retained_.find(key) == retained_.end() && retained_.size() >= retainSlots_) {
        dropped_++;
        LOG_W("broker: retain store full (%u), dropping %s", retainSlots_, topic);
        return;
    }
    retained_[key] = String(payload);
}

void EssioBroker::replayRetained(const char* filter) {
    for (const auto& kv : retained_) {
        if (!PicoMQTT::Subscriber::topic_matches(filter, kv.first.c_str())) continue;
        publish(kv.first.c_str(), (const void*)kv.second.c_str(), kv.second.length(), (uint8_t)0, true);
        replays_++;
    }
}

// ---- BrokerService ----

void BrokerService::begin(ConfigStore& store) {
    store_ = &store;
    end();

    const auto& b = store.get().broker;
    port_ = b.port;
    broker_ = new EssioBroker(b.port, b.username, b.password, b.maxClients, b.retainSlots);

    // retain 보관 + 웹 실시간 뷰(MqttMonitor)용 단일 구독. fire_message_callbacks는 첫 일치 콜백만
    // 부르므로 여기서 "#" 하나만 걸고 내부에서 분기한다. 읽은 바이트와 남은 바이트 모두
    // 구독자에게 그대로 전달된다(IncomingPublish 소멸자가 나머지를 흘려보냄).
    broker_->subscribe("#", [this](char* topic, PicoMQTT::IncomingPacket& packet) {
        broker_->countMessage();
        const bool retain = packet.get_flags() & 0x01;

        size_t len = packet.get_remaining_size();
        const size_t want = retain ? min(len, EssioBroker::MAX_RETAINED_PAYLOAD) : min(len, MqttMonitor::PAYLOAD_LEN - 1);
        char buf[EssioBroker::MAX_RETAINED_PAYLOAD + 1];
        size_t got = 0;
        while (got < want) {
            int r = packet.read((uint8_t*)buf + got, want - got);
            if (r <= 0) break;
            got += r;
        }
        buf[got] = '\0';
        mqttMonitor.record(MqttDir::Rx, topic, buf, len, retain);

        if (!retain) return;  // retain 아님 → 보관 대상 아님
        if (len > EssioBroker::MAX_RETAINED_PAYLOAD) {
            broker_->captureRetained(topic, nullptr, len);  // 크기 초과 기록만
            return;
        }
        broker_->captureRetained(topic, buf, got);
    });

    broker_->begin();
    LOG_I("broker: listening on :%u (max %u clients, retain %u slots, auth %s)", b.port, b.maxClients,
          b.retainSlots, b.username.length() ? "on" : "off");
}

void BrokerService::end() {
    if (!broker_) return;
    mqttMonitor.clearClients();
    broker_->stop();
    delete broker_;
    broker_ = nullptr;
}

void BrokerService::tick() {
    if (broker_) broker_->loop();
}

bool BrokerService::publish(const String& topic, const String& payload, bool retain) {
    if (!broker_) return false;
    if (retain) broker_->captureRetained(topic.c_str(), payload.c_str(), payload.length());
    mqttMonitor.record(MqttDir::Tx, topic.c_str(), payload.c_str(), payload.length(), retain);
    return broker_->publish(topic.c_str(), (const void*)payload.c_str(), payload.length(), (uint8_t)0, retain);
}

void BrokerService::publishOwnStatus(const String& baseTopic, bool online) {
    publish(baseTopic + "/status", online ? "online" : "offline", true);
}

void BrokerService::publishOwnDiag(const String& baseTopic, const String& payload) {
    publish(baseTopic + "/diag/info", payload, true);
}

void BrokerService::statusJson(JsonObject out) const {
    out["active"] = broker_ != nullptr;
    out["port"] = port_;
    if (!broker_) return;
    out["clients"] = broker_->clientCount();
    out["retained"] = broker_->retainedCount();
    out["messages"] = broker_->messages();
    out["replays"] = broker_->replays();
    out["rejected"] = broker_->rejected();
    out["dropped"] = broker_->dropped();
}

}  // namespace essio
