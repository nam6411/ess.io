#include "MqttMonitor.h"

namespace essio {

MqttMonitor mqttMonitor;

void MqttMonitor::begin() {
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
}

void MqttMonitor::record(MqttDir dir, const char* topic, const char* payload, size_t len, bool retain, bool ok) {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;
    if (dir == MqttDir::Rx) received_++;
    Entry& e = entries_[head_];
    e.seq = nextSeq_++;
    e.ms = millis();
    e.dir = dir;
    e.retain = retain;
    e.ok = ok;
    e.size = len > 0xFFFF ? 0xFFFF : len;
    strlcpy(e.topic, topic ? topic : "", TOPIC_LEN);
    size_t n = payload ? min(len, PAYLOAD_LEN - 1) : 0;
    memcpy(e.payload, payload, n);
    e.payload[n] = '\0';
    head_ = (head_ + 1) % CAPACITY;
    if (count_ < CAPACITY) count_++;
    xSemaphoreGive(mutex_);
}

void MqttMonitor::messagesJson(JsonDocument& doc, uint32_t sinceSeq) const {
    JsonArray msgs = doc["messages"].to<JsonArray>();
    uint32_t next = sinceSeq;
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t start = (head_ + CAPACITY - count_) % CAPACITY;
        // 한 응답을 작게 유지한다: 50개 전부(≈15KB)를 한 번에 만들면 디스플레이처럼 내부 RAM이 빠듯한
        // 보드에서 응답이 끝나지 않았다. 웹 UI는 1초마다 next부터 다시 요청하므로 금방 따라잡는다.
        size_t added = 0;
        for (size_t i = 0; i < count_; i++) {
            const Entry& e = entries_[(start + i) % CAPACITY];
            if (e.seq <= sinceSeq) continue;
            if (added++ == PAGE_MAX) {
                doc["more"] = true;
                break;
            }
            JsonObject o = msgs.add<JsonObject>();
            o["seq"] = e.seq;
            o["ms"] = e.ms;
            o["dir"] = e.dir == MqttDir::Tx ? "tx" : "rx";
            o["topic"] = e.topic;
            o["payload"] = e.payload;
            o["size"] = e.size;
            if (e.retain) o["retain"] = true;
            if (!e.ok) o["failed"] = true;
            next = e.seq;
        }
        xSemaphoreGive(mutex_);
    }
    doc["next"] = next;
}

void MqttMonitor::clientConnected(const char* clientId) {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;
    size_t i = 0;
    while (i < clientCount_ && clients_[i].id != clientId) i++;
    if (i == clientCount_ && clientCount_ < MAX_CLIENTS) clientCount_++;
    if (i < clientCount_) clients_[i] = {String(clientId), millis()};
    xSemaphoreGive(mutex_);
}

void MqttMonitor::clientDisconnected(const char* clientId) {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;
    for (size_t i = 0; i < clientCount_; i++) {
        if (clients_[i].id != clientId) continue;
        clients_[i] = clients_[--clientCount_];
        break;
    }
    xSemaphoreGive(mutex_);
}

void MqttMonitor::clearClients() {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;
    clientCount_ = 0;
    xSemaphoreGive(mutex_);
}

void MqttMonitor::clientsJson(JsonArray out) const {
    if (!mutex_ || xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return;
    uint32_t now = millis();
    for (size_t i = 0; i < clientCount_; i++) {
        JsonObject o = out.add<JsonObject>();
        o["id"] = clients_[i].id;
        o["connected_s"] = (now - clients_[i].sinceMs) / 1000;
    }
    xSemaphoreGive(mutex_);
}

}  // namespace essio
