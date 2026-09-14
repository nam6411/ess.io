#pragma once
#include "RtuSwBase.h"

// Mk2: FC03 readHoldingRegisters(1, N) / FC06 writeSingleRegister(ch, 0x0100 ON | 0x0200 OFF).
// docs/06-device-rtusw-mk2.md
namespace essio {

class RtuSwMk2 : public RtuSwBase {
public:
    const char* type() const override { return "rtusw_mk2"; }

protected:
    uint8_t maxChannels() const override { return 4; }

    MbResult readAll() override {
        uint16_t regs[MAX_CH] = {0};
        MbResult r = port_->modbus().readHoldingRegisters(slaveId_, 0x0001, channelCount_, regs);
        if (r != MbResult::Ok) return r;
        for (uint8_t i = 0; i < channelCount_; i++) state_[i] = regs[i] != 0;
        return MbResult::Ok;
    }

    MbResult writeOne(uint8_t ch, bool on) override {
        return port_->modbus().writeSingleRegister(slaveId_, ch, on ? 0x0100 : 0x0200);
    }
};

}  // namespace essio
