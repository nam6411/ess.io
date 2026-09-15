#pragma once
#include "../IDeviceModule.h"

// EPEVER UPower 인버터/충전 컨트롤러. 스펙: docs/03-device-upower.md
namespace essio {

class Upower : public IDeviceModule {
public:
    struct Power {
        float voltage = 0, current = 0, wattage = 0, apparentPower = 0;
        float temp = 0, accumulate = 0, freq = 0;
        int state = 0, soc = 0;
    };

    enum Switch : uint8_t { INVERTER = 0, GRID_PRIO, SOLAR_CHARGE, GRID_CHARGE, NUM_SWITCH };

    const char* type() const override { return "upower"; }
    const char* manufacturer() const override { return "EPEVER"; }
    const char* model() const override { return "UP5000-M6342"; }
    bool begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) override;
    PollResult pollStep() override;
    const char* lastError() const override { return lastError_; }
    void toJson(JsonObject out) const override;

    size_t switchCount() const override { return NUM_SWITCH; }
    const SwitchDef* switchDef(size_t i) const override;
    bool switchState(size_t i) const override { return i < NUM_SWITCH && switchState_[i]; }
    bool writeSwitch(size_t i, bool on) override;

    size_t sensorCount() const override;
    const SensorDef* sensorDef(size_t i) const override;

private:
    // 폴링 단계: 호출마다 Modbus 요청 하나만 수행한다.
    enum Step : uint8_t {
        STEP_COIL_INVERTER = 0,
        STEP_COIL_GRID_PRIO,
        STEP_COIL_SOLAR_CHARGE,
        STEP_COIL_GRID_CHARGE,
        STEP_DISCRETE,
        STEP_BLOCK_A,
        STEP_BLOCK_B,
        STEP_BLOCK_C,
        STEP_BLOCK_D,
        STEP_COUNT
    };
    static constexpr uint16_t COIL_ADDR[NUM_SWITCH] = {0x0106, 0x0104, 0x010B, 0x010C};

    bool readCoil(uint8_t sw);
    bool readDiscreteInputs();
    bool readBlock(uint16_t addr, uint16_t count, uint16_t* out);
    void parseBlockA(const uint16_t* r);
    void parseBlockB(const uint16_t* r);
    void parseBlockC(const uint16_t* r);
    void parseBlockD(const uint16_t* r);
    bool sensorEnabled(size_t i) const;

    Power pvIn_, pvCharge_, gridIn_, gridCharge_, inverterIn_, inverterOut_, bypass_, battery_;
    bool switchState_[NUM_SWITCH] = {false, false, false, false};
    bool bypassActive_ = false;
    bool isDay_ = false;
    bool discreteStateValid_ = false;

    struct {
        bool grid = true, pv = true, inverter = true, battery = true;
        uint8_t writeRetries = 3;
        bool maskInactiveOutput = false;
    } params_;

    uint8_t step_ = STEP_COIL_INVERTER;
    uint8_t stepErrors_ = 0;
    char lastError_[48] = {0};
};

}  // namespace essio
