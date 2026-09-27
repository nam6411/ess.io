#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

#include "../modules/IDeviceModule.h"
#include "../port/SerialPort.h"
#include "Config.h"
#include "MqttManager.h"

// 슬롯 생명주기 + 폴링 스케줄 + 스위치 명령 큐. docs/11-architecture.md §3.3, §4, §6
namespace essio {

struct Slot {
    uint8_t index = 0;
    bool enabled = false;
    String type;
    String slug;
    String label;
    uint8_t port = 0;
    uint8_t slaveId = 0;
    uint32_t pollIntervalMs = 5000;
    IDeviceModule* module = nullptr;

    uint32_t nextPollMs = 0;
    bool polling = false;       // pollStep이 Busy를 반환한 뒤 이어서 호출 중
    bool online = false;
    uint16_t errors = 0;        // 연속 오류
    uint32_t totalErrors = 0;
    uint32_t lastOkMs = 0;
    bool dirty = false;         // 상태 변경 → 발행 필요
};

struct SwitchCommand {
    uint8_t slot;
    char name[24];
    bool on;
};

class Scheduler {
public:
    static constexpr uint16_t OFFLINE_AFTER_ERRORS = 3;
    static constexpr uint32_t PORT_LOCK_WAIT_MS = 50;

    // activate=false면 포트·슬롯을 만들지 않는다 (Broker 역할: UART을 점유하지 않고 조회만 가능)
    void begin(ConfigStore& store, SerialPort* ports, MqttManager& mqtt, bool activate = true);
    void applyPorts();           // ports 변경 시 (슬롯도 전부 재생성)
    void applySlots();           // slots 변경 시
    void tick();

    // 스레드 안전 (웹 태스크에서 호출 가능)
    bool enqueueSwitch(uint8_t slot, const char* name, bool on);
    void requestPoll(uint8_t slot);
    void snapshot(JsonDocument& doc);          // GET /api/slots
    bool switchState(uint8_t slot, const char* name, bool& out);

    Slot* slot(uint8_t i) { return i < MAX_SLOTS ? &slots_[i] : nullptr; }
    uint8_t slotCount() const { return MAX_SLOTS; }
    SerialPort* port(uint8_t i) { return i < MAX_PORTS ? &ports_[i] : nullptr; }

    // 슬롯의 토픽 접두. 보드에 슬롯이 하나뿐이면 slug 구간을 생략해
    // 설계서 §3.4의 rv/<node>/... 형태가 되도록 한다 (rv/upower/upower/... 회피).
    String slotPrefix(const Slot& s) const;
    uint8_t enabledCount() const;

    void publishSlot(Slot& s);   // state + switch state 발행
    void publishAll();

private:
    void buildSlot(uint8_t i);
    void destroySlot(uint8_t i);
    void runPoll(Slot& s);
    void runCommand(const SwitchCommand& cmd);
    void slotJson(const Slot& s, JsonObject o);
    void publishSwitch(Slot& s, size_t idx);
    void publishAvailability(Slot& s, bool online);

    ConfigStore* store_ = nullptr;
    SerialPort* ports_ = nullptr;
    MqttManager* mqtt_ = nullptr;
    Slot slots_[MAX_SLOTS];
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    uint8_t rr_ = 0;  // 라운드로빈 시작 인덱스
};

}  // namespace essio
