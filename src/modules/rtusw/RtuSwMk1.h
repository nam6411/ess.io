#pragma once
#include "RtuSwBase.h"

// Mk1: FC01 readCoils(0, N) / FC05 writeSingleCoil(ch-1). docs/05-device-rtusw-mk1.md
namespace essio {

class RtuSwMk1 : public RtuSwBase {
public:
    const char* type() const override { return "rtusw_mk1"; }

protected:
    uint8_t maxChannels() const override { return 8; }

    MbResult readAll() override {
        uint8_t bits = 0;
        MbResult r = port_->modbus().readCoils(slaveId_, 0x0000, channelCount_, &bits);
        if (r != MbResult::Ok) return r;
        for (uint8_t i = 0; i < channelCount_; i++) state_[i] = (bits >> i) & 0x01;
        return MbResult::Ok;
    }

    MbResult writeOne(uint8_t ch, bool on) override {
        return port_->modbus().writeSingleCoil(slaveId_, ch - 1, on);
    }
};

}  // namespace essio
