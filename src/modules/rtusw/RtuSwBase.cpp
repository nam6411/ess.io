#include "RtuSwBase.h"

namespace essio {

bool RtuSwBase::begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) {
    port_ = &port;
    slaveId_ = slaveId;
    writeRetries_ = params["write_retries"] | 3;
    channelCount_ = 0;
    activeCount_ = 0;
    for (uint8_t i = 0; i < MAX_CH; i++) {
        enabled_[i] = false;
        state_[i] = false;
        snprintf(switchNames_[i], sizeof(switchNames_[i]), "ch%u", i + 1);
        snprintf(names_[i], sizeof(names_[i]), "Channel %u", i + 1);
        defs_[i] = {switchNames_[i], names_[i]};
    }
    for (JsonObjectConst c : params["channels"].as<JsonArrayConst>()) {
        uint8_t ch = c["ch"] | 0;
        if (ch < 1 || ch > maxChannels()) continue;
        enabled_[ch - 1] = c["enabled"] | true;
        const char* name = c["name"];
        if (name) strlcpy(names_[ch - 1], name, sizeof(names_[ch - 1]));
        if (ch > channelCount_) channelCount_ = ch;
    }
    if (channelCount_ == 0) channelCount_ = maxChannels();
    for (uint8_t i = 0; i < channelCount_; i++) {
        if (enabled_[i]) activeChannels_[activeCount_++] = i;
    }
    return true;
}

PollResult RtuSwBase::pollStep() {
    MbResult r = readAll();
    if (r != MbResult::Ok) {
        snprintf(lastError_, sizeof(lastError_), "read %s", ModbusRtu::resultName(r));
        return PollResult::Error;
    }
    return PollResult::Done;
}

bool RtuSwBase::writeSwitch(size_t i, bool on) {
    if (i >= activeCount_) return false;
    uint8_t channel = activeChannels_[i];
    for (uint8_t attempt = 0; attempt <= writeRetries_; attempt++) {
        MbResult r = writeOne(channel + 1, on);
        if (r == MbResult::Ok) {
            state_[channel] = on;
            return true;
        }
        snprintf(lastError_, sizeof(lastError_), "write ch%u %s", (unsigned)(channel + 1), ModbusRtu::resultName(r));
        delay(50);
    }
    return false;
}

void RtuSwBase::toJson(JsonObject out) const {
    JsonArray ch = out["ch"].to<JsonArray>();
    for (uint8_t i = 0; i < activeCount_; i++) ch.add(state_[activeChannels_[i]]);
}

}  // namespace essio
