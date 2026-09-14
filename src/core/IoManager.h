#pragma once
#include <Arduino.h>

#include "Config.h"
#include "Scheduler.h"

// 물리 버튼(디바운스, 짧게/길게) → 스위치 명령, 출력 핀 ← 스위치 상태. docs/10 F-IO
namespace essio {

class IoManager {
public:
    void begin(ConfigStore& store, Scheduler& scheduler);
    void applyConfig();
    void tick();

private:
    struct ButtonState {
        bool rawLast = false;
        bool stable = false;
        uint32_t changeMs = 0;
        uint32_t pressMs = 0;
        bool longFired = false;
    };

    void fire(const SwitchRef& ref, const String& mode);

    ConfigStore* store_ = nullptr;
    Scheduler* scheduler_ = nullptr;
    ButtonState buttons_[MAX_BUTTONS];
    uint32_t lastOutputMs_ = 0;
};

}  // namespace essio
