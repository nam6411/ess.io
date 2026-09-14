#pragma once
#include "../IDeviceModule.h"

// Mk1/Mk2 공통: 채널 이름/활성 파라미터, 스위치 정의, 상태 캐시.
// docs/05-device-rtusw-mk1.md, docs/06-device-rtusw-mk2.md
namespace essio {

class RtuSwBase : public IDeviceModule {
public:
    static constexpr uint8_t MAX_CH = 8;

    bool begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) override;
    const char* lastError() const override { return lastError_; }
    void toJson(JsonObject out) const override;

    size_t switchCount() const override { return channelCount_; }
    const SwitchDef* switchDef(size_t i) const override { return i < channelCount_ ? &defs_[i] : nullptr; }
    bool switchState(size_t i) const override { return i < channelCount_ && state_[i]; }
    bool writeSwitch(size_t i, bool on) override;

protected:
    virtual uint8_t maxChannels() const = 0;
    virtual MbResult readAll() = 0;                       // state_[] 갱신
    virtual MbResult writeOne(uint8_t ch, bool on) = 0;   // ch: 1-based

    PollResult pollStep() override;

    uint8_t channelCount_ = 0;      // 설정된 최대 채널 번호 (1..max)
    bool enabled_[MAX_CH] = {};
    bool state_[MAX_CH] = {};
    char names_[MAX_CH][20] = {};
    char switchNames_[MAX_CH][8] = {};
    SwitchDef defs_[MAX_CH] = {};
    uint8_t writeRetries_ = 3;
    char lastError_[48] = {0};
};

}  // namespace essio
