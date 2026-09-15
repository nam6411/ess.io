#include "Upower.h"

#include "../../core/Logger.h"

namespace essio {

namespace {

const SwitchDef SWITCHES[Upower::NUM_SWITCH] = {
    {"inverter", "Inverter"},
    {"gridout_prio", "Grid Output Priority"},
    {"solar_charge", "Solar Charge"},
    {"grid_charge", "Grid Charge"},
};

// docs/07-mqtt-homeassistant.md §B.5 Upower
const SensorDef SENSORS[] = {
    {"pv_in_v", "PV In Voltage", "V", "voltage", "measurement", "pv.in_v"},
    {"pv_in_a", "PV In Current", "A", "current", "measurement", "pv.in_a"},
    {"pv_in_w", "PV In Power", "W", "power", "measurement", "pv.in_w"},
    {"pv_chg_v", "PV Charge Voltage", "V", "voltage", "measurement", "pv.chg_v"},
    {"pv_chg_a", "PV Charge Current", "A", "current", "measurement", "pv.chg_a"},
    {"pv_chg_w", "PV Charge Power", "W", "power", "measurement", "pv.chg_w"},
    {"pv_kwh", "PV Energy", "kWh", "energy", "total_increasing", "pv.kwh"},
    {"pv_temp", "PV Temperature", "°C", "temperature", "measurement", "pv.temp"},
    {"pv_state", "PV Charge State", "", "", "", "pv.state"},
    {"pv_day", "PV Day", "", "", "", "pv.day"},
    {"grid_in_v", "Grid In Voltage", "V", "voltage", "measurement", "grid.in_v"},
    {"grid_chg_v", "Grid Charge Voltage", "V", "voltage", "measurement", "grid.chg_v"},
    {"grid_chg_a", "Grid Charge Current", "A", "current", "measurement", "grid.chg_a"},
    {"grid_chg_w", "Grid Charge Power", "W", "power", "measurement", "grid.chg_w"},
    {"grid_kwh", "Grid Energy", "kWh", "energy", "total_increasing", "grid.kwh"},
    {"grid_temp", "Grid Temperature", "°C", "temperature", "measurement", "grid.temp"},
    {"inv_in_v", "Inverter In Voltage", "V", "voltage", "measurement", "inv.in_v"},
    {"inv_out_v", "Inverter Out Voltage", "V", "voltage", "measurement", "inv.out_v"},
    {"inv_out_a", "Inverter Out Current", "A", "current", "measurement", "inv.out_a"},
    {"inv_out_va", "Inverter Output Apparent Power", "VA", "apparent_power", "measurement", "inv.out_va"},
    {"inv_hz", "Inverter Frequency", "Hz", "frequency", "measurement", "inv.hz"},
    {"bypass_v", "Bypass Voltage", "V", "voltage", "measurement", "bypass.v"},
    {"bypass_a", "Bypass Current", "A", "current", "measurement", "bypass.a"},
    {"bypass_w", "Bypass Power", "W", "power", "measurement", "bypass.w"},
    {"bypass_active", "Bypass Active", "", "", "", "bypass.active"},
    {"bat_v", "Battery Voltage", "V", "voltage", "measurement", "bat.v"},
    {"bat_temp", "Battery Temperature", "°C", "temperature", "measurement", "bat.temp"},
    {"bat_soc", "Battery SOC", "%", "battery", "measurement", "bat.soc"},
    {"bat_state", "Battery State", "", "", "", "bat.state"},
};

inline float u16(uint16_t v) { return v / 100.0f; }
inline float s16(uint16_t v) { return (int16_t)v / 100.0f; }
inline float u32(uint16_t lo, uint16_t hi) { return ((uint32_t)hi << 16 | lo) / 100.0f; }

}  // namespace

bool Upower::begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) {
    port_ = &port;
    slaveId_ = slaveId;
    JsonVariantConst blocks = params["blocks"];
    params_.grid = blocks["grid"] | true;
    params_.pv = blocks["pv"] | true;
    params_.inverter = blocks["inverter"] | true;
    params_.battery = blocks["battery"] | true;
    params_.writeRetries = params["write_retries"] | 3;
    // 이전 키는 기존 설정 파일과의 호환을 위해 한동안 fallback으로만 받는다.
    params_.maskInactiveOutput = params["mask_inactive_output"] | (params["mask_by_grid_prio"] | false);
    step_ = STEP_COIL_INVERTER;
    stepErrors_ = 0;
    discreteStateValid_ = false;
    return true;
}

const SwitchDef* Upower::switchDef(size_t i) const {
    return i < NUM_SWITCH ? &SWITCHES[i] : nullptr;
}

size_t Upower::sensorCount() const {
    size_t count = 0;
    for (size_t i = 0; i < sizeof(SENSORS) / sizeof(SENSORS[0]); i++) {
        if (sensorEnabled(i)) count++;
    }
    return count;
}

const SensorDef* Upower::sensorDef(size_t i) const {
    for (size_t raw = 0; raw < sizeof(SENSORS) / sizeof(SENSORS[0]); raw++) {
        if (!sensorEnabled(raw)) continue;
        if (i-- == 0) return &SENSORS[raw];
    }
    return nullptr;
}

bool Upower::sensorEnabled(size_t i) const {
    if (i <= 9) return params_.pv;
    if (i <= 15) return params_.grid;
    if (i <= 20) return params_.inverter;
    return params_.battery;  // block D: bypass + battery
}

bool Upower::readCoil(uint8_t sw) {
    uint8_t bits = 0;
    MbResult r = port_->modbus().readCoils(slaveId_, COIL_ADDR[sw], 1, &bits);
    if (r != MbResult::Ok) {
        snprintf(lastError_, sizeof(lastError_), "coil %04X %s", COIL_ADDR[sw], ModbusRtu::resultName(r));
        return false;
    }
    switchState_[sw] = bits & 0x01;
    return true;
}

bool Upower::readBlock(uint16_t addr, uint16_t count, uint16_t* out) {
    MbResult r = port_->modbus().readInputRegisters(slaveId_, addr, count, out);
    if (r != MbResult::Ok) {
        snprintf(lastError_, sizeof(lastError_), "block %04X %s", addr, ModbusRtu::resultName(r));
        return false;
    }
    return true;
}

bool Upower::readDiscreteInputs() {
    uint8_t bits = 0;
    MbResult r = port_->modbus().readDiscreteInputs(slaveId_, 0x2100, 2, &bits);
    if (r != MbResult::Ok) {
        discreteStateValid_ = false;
        snprintf(lastError_, sizeof(lastError_), "discrete 2100 %s", ModbusRtu::resultName(r));
        return false;
    }
    bypassActive_ = bits & 0x01;
    isDay_ = bits & 0x02;
    discreteStateValid_ = true;
    return true;
}

// docs/03-device-upower.md §3.1
void Upower::parseBlockA(const uint16_t* r) {
    gridIn_.voltage = u16(r[0]);
    gridCharge_.voltage = u16(r[5]);
    gridCharge_.current = u16(r[6]);
    gridCharge_.wattage = u32(r[7], r[8]);
    gridCharge_.accumulate = u32(r[15], r[16]);
    gridCharge_.temp = s16(r[18]);
}

// §3.2
void Upower::parseBlockB(const uint16_t* r) {
    pvIn_.voltage = u16(r[0]);
    pvIn_.current = u16(r[1]);
    pvIn_.wattage = u32(r[2], r[3]);
    pvCharge_.voltage = u16(r[4]);
    pvCharge_.current = u16(r[5]);
    pvCharge_.wattage = u32(r[6], r[7]);
    pvCharge_.accumulate = u32(r[14], r[15]);
    pvCharge_.state = (r[16] >> 2) & 0x03;
    pvCharge_.temp = s16(r[19]);
}

// §3.3
void Upower::parseBlockC(const uint16_t* r) {
    inverterIn_.voltage = u16(r[0]);
    inverterOut_.voltage = u16(r[4]);
    inverterOut_.current = u16(r[5]);
    inverterOut_.apparentPower = u32(r[7], r[8]);
    inverterOut_.freq = u16(r[12]);
}

// §3.4
void Upower::parseBlockD(const uint16_t* r) {
    battery_.voltage = u16(r[0]);
    battery_.temp = s16(r[3]);
    battery_.soc = r[4];
    battery_.state = r[7];
    bypass_.voltage = u16(r[12]);
    bypass_.current = u16(r[13]);
    bypass_.wattage = u32(r[14], r[15]);
}

PollResult Upower::pollStep() {
    bool ok = true;
    uint16_t regs[20];

    switch (step_) {
        case STEP_COIL_INVERTER:
        case STEP_COIL_GRID_PRIO:
        case STEP_COIL_SOLAR_CHARGE:
        case STEP_COIL_GRID_CHARGE:
            ok = readCoil(step_ - STEP_COIL_INVERTER);
            break;
        case STEP_DISCRETE:
            ok = readDiscreteInputs();
            break;
        case STEP_BLOCK_A:
            if (params_.grid && (ok = readBlock(0x3500, 19, regs))) parseBlockA(regs);
            break;
        case STEP_BLOCK_B:
            if (params_.pv && (ok = readBlock(0x3519, 20, regs))) parseBlockB(regs);
            break;
        case STEP_BLOCK_C:
            if (params_.inverter && (ok = readBlock(0x352F, 13, regs))) parseBlockC(regs);
            break;
        case STEP_BLOCK_D:
            if (params_.battery && (ok = readBlock(0x354C, 16, regs))) parseBlockD(regs);
            break;
        default:
            break;
    }
    if (!ok) stepErrors_++;

    if (++step_ < STEP_COUNT) return PollResult::Busy;
    step_ = STEP_COIL_INVERTER;

    // 출력 우선순위 설정(0x0104)이 아니라 실제 바이패스 상태(0x2100)를 사용한다.
    if (params_.maskInactiveOutput && discreteStateValid_) {
        if (!bypassActive_) bypass_ = Power{};
        else {
            inverterOut_.voltage = 0;
            inverterOut_.current = 0;
            inverterOut_.apparentPower = 0;
        }
    }

    bool anyError = stepErrors_ > 0;
    stepErrors_ = 0;
    return anyError ? PollResult::Error : PollResult::Done;
}

bool Upower::writeSwitch(size_t i, bool on) {
    if (i >= NUM_SWITCH) return false;
    for (uint8_t attempt = 0; attempt <= params_.writeRetries; attempt++) {
        MbResult r = port_->modbus().writeSingleCoil(slaveId_, COIL_ADDR[i], on);
        if (r == MbResult::Ok) {
            switchState_[i] = on;
            return true;
        }
        snprintf(lastError_, sizeof(lastError_), "write %04X %s", COIL_ADDR[i], ModbusRtu::resultName(r));
        delay(50);
    }
    return false;
}

void Upower::toJson(JsonObject out) const {
    if (params_.pv) {
        JsonObject pv = out["pv"].to<JsonObject>();
        pv["in_v"] = pvIn_.voltage; pv["in_a"] = pvIn_.current; pv["in_w"] = pvIn_.wattage;
        pv["chg_v"] = pvCharge_.voltage; pv["chg_a"] = pvCharge_.current; pv["chg_w"] = pvCharge_.wattage;
        pv["kwh"] = pvCharge_.accumulate; pv["temp"] = pvCharge_.temp; pv["state"] = pvCharge_.state;
        pv["day"] = isDay_;
    }
    if (params_.grid) {
        JsonObject grid = out["grid"].to<JsonObject>();
        grid["in_v"] = gridIn_.voltage;
        grid["chg_v"] = gridCharge_.voltage; grid["chg_a"] = gridCharge_.current; grid["chg_w"] = gridCharge_.wattage;
        grid["kwh"] = gridCharge_.accumulate; grid["temp"] = gridCharge_.temp;
    }
    if (params_.inverter) {
        JsonObject inv = out["inv"].to<JsonObject>();
        inv["in_v"] = inverterIn_.voltage; inv["out_v"] = inverterOut_.voltage;
        inv["out_a"] = inverterOut_.current; inv["out_va"] = inverterOut_.apparentPower; inv["hz"] = inverterOut_.freq;
    }
    if (params_.battery) {
        JsonObject bypass = out["bypass"].to<JsonObject>();
        bypass["v"] = bypass_.voltage; bypass["a"] = bypass_.current; bypass["w"] = bypass_.wattage;
        bypass["active"] = bypassActive_;
        JsonObject bat = out["bat"].to<JsonObject>();
        bat["v"] = battery_.voltage; bat["temp"] = battery_.temp; bat["soc"] = battery_.soc; bat["state"] = battery_.state;
    }
}

}  // namespace essio
