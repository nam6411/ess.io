#pragma once
#include "../IDeviceModule.h"

// JBD 스마트 BMS. 스펙: docs/04-device-jbdbms.md
namespace essio {

class Jbdbms : public IDeviceModule {
public:
    static constexpr uint8_t MAX_CELLS = 32;
    static constexpr uint8_t MAX_NTC = 8;

    enum Switch : uint8_t { CHARGE_FET = 0, DISCHARGE_FET, NUM_SWITCH };

    const char* type() const override { return "jbdbms"; }
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
    enum Step : uint8_t { STEP_BASIC = 0, STEP_CELLS, STEP_COUNT };

    // 요청 전송 + 응답 수신/검증. 성공 시 DATA 포인터/길이 반환
    bool request(uint8_t cmd, const uint8_t* data, uint8_t dataLen, const uint8_t*& outData, uint8_t& outLen);
    void parseBasic(const uint8_t* d, uint8_t len);
    void parseCells(const uint8_t* d, uint8_t len);
    bool setMosfet(bool charge, bool discharge);

    struct {
        float packV = 0, current = 0, remainAh = 0, fullAh = 0, cellDiff = 0;
        uint16_t cycles = 0, protection = 0;
        uint32_t balance = 0;
        uint8_t soc = 0, cellCount = 0, ntcCount = 0, swVersion = 0;
        bool chgFet = false, disFet = false;
        float cellV[MAX_CELLS] = {};
        float ntcC[MAX_NTC] = {};
    } st_;

    struct {
        uint8_t cellCount = 16;
        bool exposeCells = true;
        bool exposeProtection = true;
        bool chargeLimitEnabled = false;
        uint8_t socHigh = 80, socLow = 70;
    } params_;

    uint8_t rx_[128];
    uint8_t step_ = STEP_BASIC;
    uint8_t stepErrors_ = 0;
    char lastError_[48] = {0};
};

}  // namespace essio
