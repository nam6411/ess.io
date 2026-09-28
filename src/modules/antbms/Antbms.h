#pragma once
#include "../IDeviceModule.h"
#include "AntFrame.h"

// ANT BMS (UART 19200). 신형(7E A1)·구형(140바이트) 프로토콜을 모두 지원하고,
// params.protocol = "auto"(기본)면 응답하는 쪽으로 정한다. 스펙: docs/16-device-antbms.md
// 상태 JSON 키는 JBD와 맞춰 디스플레이·HA가 같은 방식으로 다룬다.
namespace essio {

class Antbms : public IDeviceModule {
public:
    enum Switch : uint8_t { CHARGE_FET = 0, DISCHARGE_FET, NUM_SWITCH };
    enum class Protocol : uint8_t { Unknown, New, Old };

    const char* type() const override { return "antbms"; }
    bool begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) override;
    PollResult pollStep() override;
    const char* lastError() const override { return lastError_; }
    void toJson(JsonObject out) const override;

    size_t switchCount() const override { return NUM_SWITCH; }
    const SwitchDef* switchDef(size_t i) const override;
    bool switchState(size_t i) const override;
    bool writeSwitch(size_t i, bool on) override;

    size_t sensorCount() const override;
    const SensorDef* sensorDef(size_t i) const override;
    const char* keySensors() const override { return "soc,power,pack_v,current,cell_diff"; }
    const char* displayRing() const override { return "battery:soc,power"; }

private:
    bool readStatus(Protocol p);
    bool readExact(uint8_t* buf, size_t n);
    void buildCellDefs();

    static constexpr size_t RX_MAX = 256;
    uint8_t rx_[RX_MAX];
    ant::Status st_;
    bool haveData_ = false;
    Protocol fixed_ = Protocol::Unknown;    // params.protocol이 new/old면 고정
    Protocol detected_ = Protocol::Unknown; // auto: 처음 응답한 쪽
    Protocol tryNext_ = Protocol::New;      // auto 탐색 중 다음에 시도할 쪽
    bool invertCurrent_ = false;
    bool exposeCells_ = true;
    char lastError_[48] = {0};

    // 셀 전압 센서 정의 (HA discovery용, 인스턴스마다)
    char cellKey_[ant::MAX_CELLS][10];
    char cellLabel_[ant::MAX_CELLS][10];
    char cellPath_[ant::MAX_CELLS][14];
    SensorDef cellDefs_[ant::MAX_CELLS];
};

}  // namespace essio
