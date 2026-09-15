#include "Jbdbms.h"

#include "../../core/Logger.h"
#include "JbdFrame.h"

namespace essio {

namespace {

const SwitchDef SWITCHES[Jbdbms::NUM_SWITCH] = {
    {"charge_fet", "Charge MOSFET"},
    {"discharge_fet", "Discharge MOSFET"},
};

// docs/07-mqtt-homeassistant.md §B.5 Jbdbms (스칼라)
const SensorDef SCALARS[] = {
    {"pack_v", "Pack Voltage", "V", "voltage", "measurement", "pack_v"},
    {"current", "Current", "A", "current", "measurement", "current"},
    {"power", "Power", "W", "power", "measurement", "power"},
    {"remain_ah", "Remaining Capacity", "Ah", "", "measurement", "remain_ah"},
    {"full_ah", "Full Capacity", "Ah", "", "measurement", "full_ah"},
    {"soc", "SOC", "%", "battery", "measurement", "soc"},
    {"cycles", "Cycles", "", "", "total_increasing", "cycles"},
    {"cell_diff", "Cell Voltage Diff", "V", "voltage", "measurement", "cell_diff"},
    {"protection", "Protection Bits", "", "", "", "protection"},
    {"balance", "Balance Bits", "", "", "", "balance"},
};
constexpr size_t NUM_SCALARS = sizeof(SCALARS) / sizeof(SCALARS[0]);

// 셀/NTC 센서 정의는 begin()에서 문자열을 채워 재사용 (모듈 인스턴스 1개당 하나씩만 유효)
struct DynDefs {
    char cellKey[Jbdbms::MAX_CELLS][12];
    char cellLabel[Jbdbms::MAX_CELLS][16];
    char cellPath[Jbdbms::MAX_CELLS][16];
    char ntcKey[Jbdbms::MAX_NTC][8];
    char ntcLabel[Jbdbms::MAX_NTC][12];
    char ntcPath[Jbdbms::MAX_NTC][12];
    SensorDef cells[Jbdbms::MAX_CELLS];
    SensorDef ntcs[Jbdbms::MAX_NTC];
    uint8_t ntcExposed = 2;
    bool built = false;
} dyn;

void buildDynDefs() {
    if (dyn.built) return;
    for (uint8_t i = 0; i < Jbdbms::MAX_CELLS; i++) {
        snprintf(dyn.cellKey[i], sizeof(dyn.cellKey[i]), "cell_%u", i + 1);
        snprintf(dyn.cellLabel[i], sizeof(dyn.cellLabel[i]), "Cell %u", i + 1);
        snprintf(dyn.cellPath[i], sizeof(dyn.cellPath[i]), "cell_v[%u]", i);
        dyn.cells[i] = {dyn.cellKey[i], dyn.cellLabel[i], "V", "voltage", "measurement", dyn.cellPath[i]};
    }
    for (uint8_t i = 0; i < Jbdbms::MAX_NTC; i++) {
        snprintf(dyn.ntcKey[i], sizeof(dyn.ntcKey[i]), "ntc_%u", i + 1);
        snprintf(dyn.ntcLabel[i], sizeof(dyn.ntcLabel[i]), "NTC %u", i + 1);
        snprintf(dyn.ntcPath[i], sizeof(dyn.ntcPath[i]), "ntc[%u]", i);
        dyn.ntcs[i] = {dyn.ntcKey[i], dyn.ntcLabel[i], "°C", "temperature", "measurement", dyn.ntcPath[i]};
    }
    dyn.built = true;
}

}  // namespace

bool Jbdbms::begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) {
    port_ = &port;
    slaveId_ = slaveId;
    params_.cellCount = params["cell_count"] | 16;
    if (params_.cellCount > MAX_CELLS) params_.cellCount = MAX_CELLS;
    params_.exposeCells = params["expose_cells"] | true;
    params_.exposeProtection = params["expose_protection_bits"] | true;
    JsonVariantConst cl = params["charge_limit"];
    params_.chargeLimitEnabled = cl["enabled"] | false;
    params_.socHigh = cl["soc_high"] | 80;
    params_.socLow = cl["soc_low"] | 70;
    dyn.ntcExposed = params["ntc_count"] | 2;
    if (dyn.ntcExposed > MAX_NTC) dyn.ntcExposed = MAX_NTC;
    buildDynDefs();
    step_ = STEP_BASIC;
    stepErrors_ = 0;
    return true;
}

const SwitchDef* Jbdbms::switchDef(size_t i) const {
    return i < NUM_SWITCH ? &SWITCHES[i] : nullptr;
}

bool Jbdbms::switchState(size_t i) const {
    if (i == CHARGE_FET) return st_.chgFet;
    if (i == DISCHARGE_FET) return st_.disFet;
    return false;
}

size_t Jbdbms::sensorCount() const {
    return NUM_SCALARS - (params_.exposeProtection ? 0 : 2) +
           (params_.exposeCells ? params_.cellCount : 0) + dyn.ntcExposed;
}

const SensorDef* Jbdbms::sensorDef(size_t i) const {
    const size_t scalars = NUM_SCALARS - (params_.exposeProtection ? 0 : 2);
    if (i < scalars) return &SCALARS[i];
    i -= scalars;
    if (params_.exposeCells) {
        if (i < params_.cellCount) return &dyn.cells[i];
        i -= params_.cellCount;
    }
    if (i < dyn.ntcExposed) return &dyn.ntcs[i];
    return nullptr;
}

// docs/04-device-jbdbms.md §2.4 (LEN 기반 수신)
bool Jbdbms::request(uint8_t cmd, const uint8_t* data, uint8_t dataLen, const uint8_t*& outData, uint8_t& outLen) {
    Stream* s = port_->stream();
    if (!s) { strlcpy(lastError_, "no stream", sizeof(lastError_)); return false; }

    uint8_t tx[16];
    size_t txLen = jbd::buildRequest(dataLen ? jbd::WRITE : jbd::READ, cmd, data, dataLen, tx);

    while (s->available()) s->read();
    s->write(tx, txLen);
    s->flush();

    size_t got = s->readBytes(rx_, 4);
    if (got < 4) { snprintf(lastError_, sizeof(lastError_), "cmd %02X: no header", cmd); return false; }
    uint8_t len = rx_[3];
    if (4 + len + 3 > sizeof(rx_)) { snprintf(lastError_, sizeof(lastError_), "cmd %02X: len %u too big", cmd, len); return false; }
    got += s->readBytes(rx_ + 4, len + 3);

    uint8_t verifiedLen = 0;
    jbd::Verify v = jbd::verifyResponse(rx_, got, verifiedLen);
    if (v != jbd::Verify::Ok) {
        snprintf(lastError_, sizeof(lastError_), "cmd %02X: verify %u", cmd, (unsigned)v);
        return false;
    }
    outData = rx_ + 4;
    outLen = verifiedLen;
    return true;
}

// §3.1
void Jbdbms::parseBasic(const uint8_t* d, uint8_t len) {
    if (len < 23) return;
    st_.packV = jbd::u16(d + 0) / 100.0f;
    st_.current = jbd::s16(d + 2) / 100.0f;
    st_.remainAh = jbd::u16(d + 4) / 100.0f;
    st_.fullAh = jbd::u16(d + 6) / 100.0f;
    st_.cycles = jbd::u16(d + 8);
    st_.balance = (uint32_t)jbd::u16(d + 12) | ((uint32_t)jbd::u16(d + 14) << 16);
    st_.protection = jbd::u16(d + 16);
    st_.swVersion = d[18];
    st_.soc = d[19];
    st_.chgFet = d[20] & 0x01;
    st_.disFet = (d[20] >> 1) & 0x01;
    st_.cellCount = d[21];
    st_.ntcCount = d[22];
    if (st_.ntcCount > MAX_NTC) st_.ntcCount = MAX_NTC;
    for (uint8_t i = 0; i < st_.ntcCount && 23 + i * 2 + 1 < len; i++) {
        st_.ntcC[i] = (jbd::u16(d + 23 + i * 2) - 2731) / 10.0f;
    }
    if (st_.cellCount != params_.cellCount) {
        LOG_W("jbdbms: cell_count %u != reported %u", params_.cellCount, st_.cellCount);
    }
}

// §3.2
void Jbdbms::parseCells(const uint8_t* d, uint8_t len) {
    uint8_t n = len / 2;
    if (n > MAX_CELLS) n = MAX_CELLS;
    float minV = 99, maxV = 0;
    for (uint8_t i = 0; i < n; i++) {
        st_.cellV[i] = jbd::u16(d + i * 2) / 1000.0f;
        if (st_.cellV[i] < minV) minV = st_.cellV[i];
        if (st_.cellV[i] > maxV) maxV = st_.cellV[i];
    }
    st_.cellDiff = n ? maxV - minV : 0;
}

PollResult Jbdbms::pollStep() {
    const uint8_t* data;
    uint8_t len;
    bool ok;

    switch (step_) {
        case STEP_BASIC:
            ok = request(jbd::CMD_BASIC, nullptr, 0, data, len);
            if (ok) parseBasic(data, len);
            break;
        case STEP_CELLS:
            ok = request(jbd::CMD_CELLS, nullptr, 0, data, len);
            if (ok) parseCells(data, len);
            break;
        default:
            ok = true;
            break;
    }
    if (!ok) stepErrors_++;

    if (++step_ < STEP_COUNT) return PollResult::Busy;
    step_ = STEP_BASIC;

    // TODO: F-JBD-5 충전 제한 (params_.chargeLimitEnabled) — soc_high 이상 chgFet OFF, soc_low 이하 ON

    bool anyError = stepErrors_ > 0;
    stepErrors_ = 0;
    return anyError ? PollResult::Error : PollResult::Done;
}

// §3.3 VAL bit0 = 충전 차단, bit1 = 방전 차단
bool Jbdbms::setMosfet(bool charge, bool discharge) {
    uint8_t val = (charge ? 0 : 1) | (discharge ? 0 : 2);
    uint8_t payload[2] = {0x00, val};
    const uint8_t* data;
    uint8_t len;
    return request(jbd::CMD_MOSFET, payload, 2, data, len);
}

bool Jbdbms::writeSwitch(size_t i, bool on) {
    bool chg = st_.chgFet, dis = st_.disFet;
    if (i == CHARGE_FET) chg = on;
    else if (i == DISCHARGE_FET) dis = on;
    else return false;
    if (!setMosfet(chg, dis)) return false;
    st_.chgFet = chg;
    st_.disFet = dis;
    return true;
}

void Jbdbms::toJson(JsonObject out) const {
    out["pack_v"] = st_.packV;
    out["current"] = st_.current;
    out["power"] = st_.packV * st_.current;
    out["remain_ah"] = st_.remainAh;
    out["full_ah"] = st_.fullAh;
    out["soc"] = st_.soc;
    out["cycles"] = st_.cycles;
    out["cell_diff"] = st_.cellDiff;
    out["chg_fet"] = st_.chgFet;
    out["dis_fet"] = st_.disFet;
    if (params_.exposeProtection) {
        out["protection"] = st_.protection;
        out["balance"] = st_.balance;
    }
    out["cell_count"] = st_.cellCount;
    JsonArray cells = out["cell_v"].to<JsonArray>();
    for (uint8_t i = 0; i < params_.cellCount; i++) cells.add(st_.cellV[i]);
    JsonArray ntc = out["ntc"].to<JsonArray>();
    for (uint8_t i = 0; i < st_.ntcCount; i++) ntc.add(st_.ntcC[i]);
}

}  // namespace essio
