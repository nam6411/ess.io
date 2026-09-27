#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// 웹 UI 실시간 MQTT 뷰용 기록 (GET /api/mqtt, docs/13-web-api.md)
// 메인 루프가 쓰고 웹 태스크(AsyncTCP)가 읽으므로 Logger와 같은 방식으로 뮤텍스를 건다.
namespace essio {

enum class MqttDir : uint8_t { Tx, Rx };

class MqttMonitor {
public:
    static constexpr size_t CAPACITY = 50;
    static constexpr size_t TOPIC_LEN = 96;
    static constexpr size_t PAYLOAD_LEN = 160;  // 초과분은 잘라서 보관, 원래 길이는 size로 남긴다
    static constexpr size_t MAX_CLIENTS = 16;

    void begin();

    void record(MqttDir dir, const char* topic, const char* payload, size_t len, bool retain, bool ok = true);
    void messagesJson(JsonDocument& doc, uint32_t sinceSeq) const;

    // Broker 역할: 접속 중인 클라이언트 목록
    void clientConnected(const char* clientId);
    void clientDisconnected(const char* clientId);
    void clearClients();
    void clientsJson(JsonArray out) const;

    uint32_t received() const { return received_; }

private:
    struct Entry {
        uint32_t seq;
        uint32_t ms;
        MqttDir dir;
        bool retain;
        bool ok;
        uint16_t size;
        char topic[TOPIC_LEN];
        char payload[PAYLOAD_LEN];
    };
    struct ClientEntry {
        String id;
        uint32_t sinceMs;
    };

    Entry entries_[CAPACITY];
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t nextSeq_ = 1;
    uint32_t received_ = 0;
    ClientEntry clients_[MAX_CLIENTS];
    size_t clientCount_ = 0;
    SemaphoreHandle_t mutex_ = nullptr;
};

extern MqttMonitor mqttMonitor;

}  // namespace essio
