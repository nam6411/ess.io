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
    {"grid_in_v", "Grid In Voltage", "V", "voltage", "measurement", "grid.in_v"},
    {"grid_in_a", "Grid In Current", "A", "current", "measurement", "grid.in_a"},
    {"grid_in_w", "Grid In Power", "W", "power", "measurement", "grid.in_w"},
    {"grid_chg_v", "Grid Charge Voltage", "V", "voltage", "measurement", "grid.chg_v"},
    {"grid_chg_a", "Grid Charge Current", "A", "current", "measurement", "grid.chg_a"},
    {"grid_chg_w", "Grid Charge Power", "W", "power", "measurement", "grid.chg_w"},
    {"grid_kwh", "Grid Energy", "kWh", "energy", "total_increasing", "grid.kwh"},
    {"grid_temp", "Grid Temperature", "°C", "temperature", "measurement", "grid.temp"},
    {"inv_in_v", "Inverter In Voltage", "V", "voltage", "measurement", "inv.in_v"},
    {"inv_out_v", "Inverter Out Voltage", "V", "voltage", "measurement", "inv.out_v"},
    {"inv_out_a", "Inverter Out Current", "A", "current", "measurement", "inv.out_a"},
    {"inv_out_w", "Inverter Out Power", "W", "power", "measurement", "inv.out_w"},
    {"inv_hz", "Inverter Frequency", "Hz", "frequency", "measurement", "inv.hz"},
    {"bypass_v", "Bypass Voltage", "V", "voltage", "measurement", "bypass.v"},
    {"bypass_a", "Bypass Current", "A", "current", "measurement", "bypass.a"},
    {"bypass_w", "Bypass Power", "W", "power", "measurement", "bypass.w"},
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
    params_.maskByGridPrio = params["mask_by_grid_prio"] | false;
    step_ = STEP_COILS;
    stepErrors_ = 0;
    return true;
}

const SwitchDef* Upower::switchDef(size_t i) const {
    return i < NUM_SWITCH ? &SWITCHES[i] : nullptr;
}

size_t Upower::sensorCount() const {
    return sizeof(SENSORS) / sizeof(SENSORS[0]);
}

const SensorDef* Upower::sensorDef(size_t i) const {
    return i < sensorCount() ? &SENSORS[i] : nullptr;
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
    pvCharge_.state = (r[16] >> 1) & 0x03;
    pvCharge_.temp = s16(r[19]);
}

// §3.3
void Upower::parseBlockC(const uint16_t* r) {
    inverterIn_.voltage = u16(r[0]);
    inverterOut_.voltage = u16(r[4]);
    inverterOut_.current = u16(r[5]);
    inverterOut_.wattage = u32(r[7], r[8]);
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
        case STEP_COILS:
            for (uint8_t i = 0; i < NUM_SWITCH; i++) ok = readCoil(i) && ok;
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
    step_ = STEP_COILS;

    // §3.5 파생값, §3.6 마스킹(옵션)
    gridIn_.current = gridCharge_.current + bypass_.current;
    gridIn_.wattage = gridCharge_.wattage + bypass_.wattage;
    if (params_.maskByGridPrio) {
        if (!switchState_[GRID_PRIO]) bypass_ = Power{};
        else { inverterOut_.voltage = inverterOut_.current = inverterOut_.wattage = 0; }
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
    JsonObject pv = out["pv"].to<JsonObject>();
    pv["in_v"] = pvIn_.voltage; pv["in_a"] = pvIn_.current; pv["in_w"] = pvIn_.wattage;
    pv["chg_v"] = pvCharge_.voltage; pv["chg_a"] = pvCharge_.current; pv["chg_w"] = pvCharge_.wattage;
    pv["kwh"] = pvCharge_.accumulate; pv["temp"] = pvCharge_.temp; pv["state"] = pvCharge_.state;

    JsonObject grid = out["grid"].to<JsonObject>();
    grid["in_v"] = gridIn_.voltage; grid["in_a"] = gridIn_.current; grid["in_w"] = gridIn_.wattage;
    grid["chg_v"] = gridCharge_.voltage; grid["chg_a"] = gridCharge_.current; grid["chg_w"] = gridCharge_.wattage;
    grid["kwh"] = gridCharge_.accumulate; grid["temp"] = gridCharge_.temp;

    JsonObject inv = out["inv"].to<JsonObject>();
    inv["in_v"] = inverterIn_.voltage; inv["out_v"] = inverterOut_.voltage;
    inv["out_a"] = inverterOut_.current; inv["out_w"] = inverterOut_.wattage; inv["hz"] = inverterOut_.freq;

    JsonObject bypass = out["bypass"].to<JsonObject>();
    bypass["v"] = bypass_.voltage; bypass["a"] = bypass_.current; bypass["w"] = bypass_.wattage;

    JsonObject bat = out["bat"].to<JsonObject>();
    bat["v"] = battery_.voltage; bat["temp"] = battery_.temp; bat["soc"] = battery_.soc; bat["state"] = battery_.state;
}

}  // namespace essio
