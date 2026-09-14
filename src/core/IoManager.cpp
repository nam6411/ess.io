#include "IoManager.h"

#include "Logger.h"

namespace essio {

void IoManager::begin(ConfigStore& store, Scheduler& scheduler) {
    store_ = &store;
    scheduler_ = &scheduler;
    applyConfig();
}

void IoManager::applyConfig() {
    const Config& cfg = store_->get();
    for (uint8_t i = 0; i < cfg.buttonCount; i++) {
        const ButtonConfig& b = cfg.buttons[i];
        if (b.pin < 0) continue;
        pinMode(b.pin, b.activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
        buttons_[i] = ButtonState();
        bool pressed = digitalRead(b.pin) == (b.activeLow ? LOW : HIGH);
        buttons_[i].rawLast = buttons_[i].stable = pressed;
        buttons_[i].changeMs = millis();
    }
    for (uint8_t i = 0; i < cfg.outputCount; i++) {
        const OutputConfig& o = cfg.outputs[i];
        if (o.pin < 0) continue;
        pinMode(o.pin, OUTPUT);
        digitalWrite(o.pin, o.activeHigh ? LOW : HIGH);
    }
}

void IoManager::fire(const SwitchRef& ref, const String& mode) {
    if (!ref.valid()) return;
    bool on = true;
    if (mode == "off") on = false;
    else if (mode == "toggle") {
        bool cur = false;
        if (!scheduler_->switchState(ref.slot, ref.name.c_str(), cur)) {
            LOG_W("io: switch %d/%s not found", ref.slot, ref.name.c_str());
            return;
        }
        on = !cur;
    }
    LOG_I("io: button -> slot %d %s %s", ref.slot, ref.name.c_str(), on ? "ON" : "OFF");
    scheduler_->enqueueSwitch(ref.slot, ref.name.c_str(), on);
}

void IoManager::tick() {
    const Config& cfg = store_->get();
    uint32_t now = millis();

    for (uint8_t i = 0; i < cfg.buttonCount; i++) {
        const ButtonConfig& b = cfg.buttons[i];
        if (b.pin < 0) continue;
        ButtonState& st = buttons_[i];
        bool raw = digitalRead(b.pin) == (b.activeLow ? LOW : HIGH);
        if (raw != st.rawLast) {
            st.rawLast = raw;
            st.changeMs = now;
        }
        if (raw != st.stable && now - st.changeMs >= b.debounceMs) {
            st.stable = raw;
            if (raw) {
                st.pressMs = now;
                st.longFired = false;
            } else if (!st.longFired) {
                fire(b.action, b.mode);
            }
        }
        if (st.stable && b.longPressMs > 0 && !st.longFired && now - st.pressMs >= b.longPressMs) {
            st.longFired = true;
            fire(b.longAction, "toggle");
        }
    }

    if (now - lastOutputMs_ >= 200) {
        lastOutputMs_ = now;
        for (uint8_t i = 0; i < cfg.outputCount; i++) {
            const OutputConfig& o = cfg.outputs[i];
            if (o.pin < 0 || !o.source.valid()) continue;
            bool on = false;
            scheduler_->switchState(o.source.slot, o.source.name.c_str(), on);
            digitalWrite(o.pin, (on == o.activeHigh) ? HIGH : LOW);
        }
    }
}

}  // namespace essio
