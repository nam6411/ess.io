#include "Antbms.h"

#include "../../core/Logger.h"

namespace essio {

namespace {

const SwitchDef SWITCHES[Antbms::NUM_SWITCH] = {
    {"charge_fet", "Charge MOSFET"},
    {"discharge_fet", "Discharge MOSFET"},
};

const SensorDef SCALARS[] = {
    {"pack_v", "Pack Voltage", "V", "voltage", "measurement", "pack_v"},
    {"current", "Current", "A", "current", "measurement", "current"},
    {"power", "Power", "W", "power", "measurement", "power"},
    {"soc", "SOC", "%", "battery", "measurement", "soc"},
    {"soh", "SOH", "%", "", "measurement", "soh"},
    {"remain_ah", "Remaining Capacity", "Ah", "", "measurement", "remain_ah"},
    {"full_ah", "Full Capacity", "Ah", "", "measurement", "full_ah"},
    {"cycle_ah", "Cycle Capacity", "Ah", "", "total_increasing", "cycle_ah"},
    {"cell_diff", "Cell Voltage Diff", "V", "voltage", "measurement", "cell_diff"},
    {"mos_temp", "MOSFET Temperature", "°C", "temperature", "measurement", "mos_temp"},
    {"bal_temp", "Balancer Temperature", "°C", "temperature", "measurement", "bal_temp"},
};
constexpr size_t NUM_SCALARS = sizeof(SCALARS) / sizeof(SCALARS[0]);

// MOSFET 상태 코드 (신형 기준, 구형도 앞부분이 같다). 0 = Off, 1 = On, 그 밖은 차단 사유
const char* const CHARGE_STATUS[] = {
    "Off", "On", "Overcharge protection", "Over current protection", "Battery full", "Total overpressure",
    "Battery over temperature", "MOSFET over temperature", "Abnormal current", "Balanced line dropped string",
    "Motherboard over temperature", "Reserved", "Open failed", "Discharge MOSFET abnormality", "Waiting",
    "Manually turned off", "Two level exceed voltage", "Low temperature protection", "Voltage difference exceeded",
    "Reserved", "Self detect error",
};
const char* const DISCHARGE_STATUS[] = {
    "Off", "On", "Overdischarge protection", "Over current protection", "Two current exceeded",
    "Total pressure undervoltage", "Battery over temperature", "MOSFET over temperature", "Abnormal current",
    "Balanced line dropped string", "Motherboard over temperature", "Charge MOSFET on", "Short circuit protection",
    "Discharge MOSFET abnormality", "Open failed", "Manually turned off", "Two level low voltage",
    "Low temperature protection", "Voltage difference exceeded", "Self detect error",
};
const char* const BALANCER_STATUS[] = {
    "Off", "Exceeds the limit equilibrium", "Charge differential pressure balance", "Balanced over temperature",
    "Automatic equalization",
};

template <size_t N>
const char* statusText(const char* const (&table)[N], uint8_t code) {
    return code < N ? table[code] : "Unknown";
}

const char* protocolName(Antbms::Protocol p) {
    return p == Antbms::Protocol::New ? "new" : p == Antbms::Protocol::Old ? "old" : "unknown";
}

}  // namespace

bool Antbms::begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) {
    port_ = &port;
    slaveId_ = slaveId;
    String proto = params["protocol"] | "auto";
    fixed_ = proto == "new" ? Protocol::New : proto == "old" ? Protocol::Old : Protocol::Unknown;
    detected_ = Protocol::Unknown;
    tryNext_ = Protocol::New;
    invertCurrent_ = params["invert_current"] | false;
    exposeCells_ = params["expose_cells"] | true;
    haveData_ = false;
    st_ = ant::Status();
    buildCellDefs();
    return true;
}

void Antbms::buildCellDefs() {
    for (uint8_t i = 0; i < ant::MAX_CELLS; i++) {
        snprintf(cellKey_[i], sizeof(cellKey_[i]), "cell_%u", i + 1);
        snprintf(cellLabel_[i], sizeof(cellLabel_[i]), "Cell %u", i + 1);
        snprintf(cellPath_[i], sizeof(cellPath_[i]), "cell_v[%u]", i);
        cellDefs_[i] = {cellKey_[i], cellLabel_[i], "V", "voltage", "measurement", cellPath_[i]};
    }
}

const SwitchDef* Antbms::switchDef(size_t i) const { return i < NUM_SWITCH ? &SWITCHES[i] : nullptr; }

bool Antbms::switchState(size_t i) const {
    if (i == CHARGE_FET) return st_.chgStatus == 1;
    if (i == DISCHARGE_FET) return st_.disStatus == 1;
    return false;
}

// 셀 수는 첫 응답에서 알게 된다. 그 전에는 스칼라만 (Discovery는 접속·Rediscover 때 다시 나간다)
size_t Antbms::sensorCount() const { return NUM_SCALARS + (exposeCells_ ? st_.cellCount : 0); }

const SensorDef* Antbms::sensorDef(size_t i) const {
    if (i < NUM_SCALARS) return &SCALARS[i];
    i -= NUM_SCALARS;
    if (exposeCells_ && i < st_.cellCount) return &cellDefs_[i];
    return nullptr;
}

bool Antbms::readExact(uint8_t* buf, size_t n) {
    return port_->stream()->readBytes(buf, n) == n;
}

// 요청 하나 보내고 상태 프레임 하나를 받아 st_에 반영
bool Antbms::readStatus(Protocol p) {
    Stream* s = port_->stream();
    if (!s) { strlcpy(lastError_, "no stream", sizeof(lastError_)); return false; }
    while (s->available()) s->read();

    uint8_t tx[10];
    ant::Status next;
    if (p == Protocol::New) {
        s->write(tx, ant::buildNewStatusRequest(tx));
        s->flush();
        if (!readExact(rx_, 6)) { strlcpy(lastError_, "new: no response", sizeof(lastError_)); return false; }
        const size_t len = ant::newFrameLength(rx_);
        if (rx_[0] != ant::NEW_HEAD1 || rx_[1] != ant::NEW_HEAD2 || len > RX_MAX) {
            snprintf(lastError_, sizeof(lastError_), "new: bad header %02X %02X", rx_[0], rx_[1]);
            return false;
        }
        if (!readExact(rx_ + 6, len - 6)) { strlcpy(lastError_, "new: short frame", sizeof(lastError_)); return false; }
        if (!ant::parseNewStatus(rx_, len, next)) {
            snprintf(lastError_, sizeof(lastError_), "new: verify %u", (unsigned)ant::verifyNew(rx_, len));
            return false;
        }
    } else {
        s->write(tx, ant::buildOldStatusRequest(tx));
        s->flush();
        if (!readExact(rx_, ant::OLD_FRAME_LEN)) { strlcpy(lastError_, "old: no response", sizeof(lastError_)); return false; }
        if (!ant::parseOldStatus(rx_, ant::OLD_FRAME_LEN, next)) {
            snprintf(lastError_, sizeof(lastError_), "old: verify %u", (unsigned)ant::verifyOld(rx_, ant::OLD_FRAME_LEN));
            return false;
        }
    }
    if (invertCurrent_) next.current = -next.current;
    st_ = next;
    haveData_ = true;
    lastError_[0] = '\0';
    return true;
}

PollResult Antbms::pollStep() {
    Protocol p = fixed_ != Protocol::Unknown ? fixed_ : detected_ != Protocol::Unknown ? detected_ : tryNext_;
    if (readStatus(p)) {
        if (fixed_ == Protocol::Unknown && detected_ == Protocol::Unknown) {
            detected_ = p;
            LOG_I("antbms: %s protocol detected, %u cells", protocolName(p), st_.cellCount);
        }
        return PollResult::Done;
    }
    // auto 탐색 중이면 다음에는 다른 프로토콜로 물어본다
    if (fixed_ == Protocol::Unknown && detected_ == Protocol::Unknown)
        tryNext_ = tryNext_ == Protocol::New ? Protocol::Old : Protocol::New;
    return PollResult::Error;
}

bool Antbms::writeSwitch(size_t i, bool on) {
    if (i >= NUM_SWITCH) return false;
    Stream* s = port_->stream();
    Protocol p = fixed_ != Protocol::Unknown ? fixed_ : detected_;
    if (!s || p == Protocol::Unknown) {
        strlcpy(lastError_, "switch: protocol not detected yet", sizeof(lastError_));
        return false;
    }
    uint8_t tx[22];
    if (p == Protocol::New) {
        // 쓰기 전에 기본 암호로 인증한다. 켜기/끄기는 서로 다른 레지스터에 0을 쓰는 방식
        s->write(tx, ant::buildNewAuth(tx));
        s->flush();
        delay(50);
        const uint8_t reg = i == CHARGE_FET ? (on ? ant::NEW_REG_CHARGE_ON : ant::NEW_REG_CHARGE_OFF)
                                            : (on ? ant::NEW_REG_DISCHARGE_ON : ant::NEW_REG_DISCHARGE_OFF);
        s->write(tx, ant::buildNewWrite(reg, tx));
    } else {
        s->write(tx, ant::buildOldSwitch(i == CHARGE_FET ? ant::OLD_REG_CHARGE : ant::OLD_REG_DISCHARGE, on, tx));
    }
    s->flush();
    delay(100);
    while (s->available()) s->read();  // ACK는 쓰지 않는다 — 스케줄러가 1초 뒤 다시 읽어 확인한다
    if (i == CHARGE_FET) st_.chgStatus = on ? 1 : 0;
    else st_.disStatus = on ? 1 : 0;
    return true;
}

void Antbms::toJson(JsonObject out) const {
    out["protocol"] = protocolName(fixed_ != Protocol::Unknown ? fixed_ : detected_);
    if (!haveData_) return;
    out["pack_v"] = st_.packV;
    out["current"] = st_.current;
    out["power"] = st_.packV * st_.current;  // + 충전, - 방전 (current 부호를 따른다)
    out["soc"] = st_.soc;
    out["soh"] = st_.soh;
    out["remain_ah"] = st_.remainAh;
    out["full_ah"] = st_.fullAh;
    out["cycle_ah"] = st_.cycleAh;
    out["cell_diff"] = st_.cellDiff;
    out["cell_max"] = st_.cellMax;
    out["cell_min"] = st_.cellMin;
    out["mos_temp"] = st_.mosTemp;
    out["bal_temp"] = st_.balTemp;
    out["chg_fet"] = st_.chgStatus == 1;
    out["dis_fet"] = st_.disStatus == 1;
    out["chg_status"] = statusText(CHARGE_STATUS, st_.chgStatus);
    out["dis_status"] = statusText(DISCHARGE_STATUS, st_.disStatus);
    out["bal_status"] = statusText(BALANCER_STATUS, st_.balStatus);
    out["runtime_s"] = st_.runtimeS;
    out["cell_count"] = st_.cellCount;
    JsonArray cells = out["cell_v"].to<JsonArray>();
    for (uint8_t i = 0; i < st_.cellCount; i++) cells.add(st_.cellV[i]);
    JsonArray temps = out["temp"].to<JsonArray>();
    for (uint8_t i = 0; i < st_.tempCount; i++) temps.add(st_.temp[i]);
}

}  // namespace essio
