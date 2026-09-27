#include "Scheduler.h"

#include "../modules/ModuleRegistry.h"
#include "Logger.h"

namespace essio {

void Scheduler::begin(ConfigStore& store, SerialPort* ports, MqttManager& mqtt, bool activate) {
    store_ = &store;
    ports_ = ports;
    mqtt_ = &mqtt;
    if (!queue_) queue_ = xQueueCreate(16, sizeof(SwitchCommand));
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        slots_[i].index = i;
        slots_[i].enabled = false;
    }
    if (activate) applyPorts();
}

void Scheduler::applyPorts() {
    for (uint8_t i = 0; i < MAX_SLOTS; i++) destroySlot(i);
    const Config& cfg = store_->get();
    for (uint8_t i = 0; i < MAX_PORTS; i++) ports_[i].begin(cfg.ports[i]);
    applySlots();
}

void Scheduler::applySlots() {
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        destroySlot(i);
        buildSlot(i);
    }
}

void Scheduler::buildSlot(uint8_t i) {
    const SlotConfig& sc = store_->get().slots[i];
    Slot& s = slots_[i];
    s.enabled = sc.enabled && sc.type != "none";
    s.type = sc.type;
    s.slug = sc.slug;
    s.label = sc.label;
    s.port = sc.port;
    s.slaveId = sc.slaveId;
    s.pollIntervalMs = sc.pollIntervalMs;
    s.online = false;
    s.errors = 0;
    s.polling = false;
    s.nextPollMs = millis() + 500 * i;  // 슬롯 간 시차
    if (!s.enabled) return;

    if (s.port >= MAX_PORTS || !ports_[s.port].isActive()) {
        LOG_E("slot %u: port %u not active", i, s.port);
        s.enabled = false;
        return;
    }
    s.module = ModuleRegistry::create(sc.type);
    if (!s.module) {
        LOG_E("slot %u: unknown type %s", i, sc.type.c_str());
        s.enabled = false;
        return;
    }
    if (!s.module->begin(ports_[s.port], s.slaveId, sc.params.as<JsonVariantConst>())) {
        LOG_E("slot %u: module begin failed", i);
        delete s.module;
        s.module = nullptr;
        s.enabled = false;
        return;
    }
    LOG_I("slot %u: %s '%s' port=%u slave=%u every %lu ms", i, s.type.c_str(), s.slug.c_str(),
          s.port, s.slaveId, (unsigned long)s.pollIntervalMs);
}

void Scheduler::destroySlot(uint8_t i) {
    Slot& s = slots_[i];
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        if (s.module) {
            s.module->end();
            delete s.module;
            s.module = nullptr;
        }
        s.enabled = false;
        s.online = false;
        xSemaphoreGive(mutex_);
    }
}

bool Scheduler::enqueueSwitch(uint8_t slot, const char* name, bool on) {
    if (!queue_ || slot >= MAX_SLOTS) return false;
    SwitchCommand cmd;
    cmd.slot = slot;
    strlcpy(cmd.name, name, sizeof(cmd.name));
    cmd.on = on;
    return xQueueSend(queue_, &cmd, 0) == pdTRUE;
}

void Scheduler::requestPoll(uint8_t slot) {
    if (slot < MAX_SLOTS) slots_[slot].nextPollMs = millis();
}

void Scheduler::tick() {
    // 1. 명령 큐 우선 처리 (docs/11 §4)
    SwitchCommand cmd;
    while (xQueueReceive(queue_, &cmd, 0) == pdTRUE) runCommand(cmd);

    // 2. 폴링: 시각이 된 슬롯 중 포트가 비어 있는 것 하나 (라운드로빈)
    uint32_t now = millis();
    for (uint8_t k = 0; k < MAX_SLOTS; k++) {
        uint8_t i = (rr_ + k) % MAX_SLOTS;
        Slot& s = slots_[i];
        if (!s.enabled || !s.module) continue;
        if (!s.polling && (int32_t)(now - s.nextPollMs) < 0) continue;
        runPoll(s);
        rr_ = (i + 1) % MAX_SLOTS;
        break;
    }

    // 3. 변경된 슬롯 발행
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        if (slots_[i].dirty) {
            publishSlot(slots_[i]);
            slots_[i].dirty = false;
        }
    }
}

void Scheduler::runPoll(Slot& s) {
    SerialPort& port = ports_[s.port];
    if (!port.lock(PORT_LOCK_WAIT_MS)) return;
    PollResult r = s.module->pollStep();
    port.unlock();

    switch (r) {
        case PollResult::Busy:
            s.polling = true;
            break;
        case PollResult::Done:
            s.polling = false;
            s.nextPollMs = millis() + s.pollIntervalMs;
            s.lastOkMs = millis();
            s.errors = 0;
            if (!s.online) {
                s.online = true;
                LOG_I("slot %u: online", s.index);
                publishAvailability(s, true);
            }
            s.dirty = true;
            break;
        case PollResult::Error:
            s.polling = false;
            s.nextPollMs = millis() + s.pollIntervalMs;
            s.errors++;
            s.totalErrors++;
            LOG_W("slot %u: poll error #%u: %s", s.index, s.errors, s.module->lastError());
            if (s.online && s.errors >= OFFLINE_AFTER_ERRORS) {
                s.online = false;
                LOG_W("slot %u: offline", s.index);
                publishAvailability(s, false);
            }
            break;
    }
}

void Scheduler::runCommand(const SwitchCommand& cmd) {
    Slot& s = slots_[cmd.slot];
    if (!s.enabled || !s.module) return;
    int idx = s.module->switchIndex(cmd.name);
    if (idx < 0) {
        LOG_W("slot %u: unknown switch '%s'", cmd.slot, cmd.name);
        return;
    }
    SerialPort& port = ports_[s.port];
    if (!port.lock(500)) {
        LOG_W("slot %u: port busy, command dropped", cmd.slot);
        return;
    }
    bool ok = s.module->writeSwitch(idx, cmd.on);
    port.unlock();
    LOG_I("slot %u: %s -> %s %s", cmd.slot, cmd.name, cmd.on ? "ON" : "OFF", ok ? "ok" : "FAILED");
    publishSwitch(s, idx);
    s.nextPollMs = millis() + 1000;  // 곧 재확인
}

// ---- MQTT 발행 (docs/07 §B.3) ----

uint8_t Scheduler::enabledCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        if (slots_[i].enabled) n++;
    }
    return n;
}

String Scheduler::slotPrefix(const Slot& s) const {
    String base = mqtt_->baseTopic();
    return enabledCount() <= 1 ? base : base + "/" + s.slug;
}

void Scheduler::publishAvailability(Slot& s, bool online) {
    if (!mqtt_->connected()) return;
    mqtt_->publish(slotPrefix(s) + "/availability", online ? "online" : "offline", true);
}

void Scheduler::publishSwitch(Slot& s, size_t idx) {
    if (!mqtt_->connected()) return;
    const SwitchDef* d = s.module->switchDef(idx);
    if (!d) return;
    mqtt_->publish(slotPrefix(s) + "/switch/" + d->name + "/state", s.module->switchState(idx) ? "ON" : "OFF", true);
}

void Scheduler::publishSlot(Slot& s) {
    if (!s.enabled || !s.module || !mqtt_->connected()) return;
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    s.module->toJson(root);
    String payload;
    serializeJson(doc, payload);
    mqtt_->publish(slotPrefix(s) + "/state", payload, true);
    for (size_t i = 0; i < s.module->switchCount(); i++) publishSwitch(s, i);
}

void Scheduler::publishAll() {
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        Slot& s = slots_[i];
        if (s.enabled && s.module) {
            publishAvailability(s, s.online);
            if (s.online) publishSlot(s);
        }
    }
}

// ---- 웹 스냅샷 ----

void Scheduler::slotJson(const Slot& s, JsonObject o) {
    o["index"] = s.index;
    o["enabled"] = s.enabled;
    o["type"] = s.type;
    o["slug"] = s.slug;
    o["label"] = s.label;
    o["online"] = s.online;
    o["errors"] = s.errors;
    o["total_errors"] = s.totalErrors;
    if (s.lastOkMs) o["last_ok_ms_ago"] = millis() - s.lastOkMs;
    else o["last_ok_ms_ago"] = nullptr;
    if (!s.module) return;
    o["last_error"] = s.module->lastError();
    s.module->toJson(o["state"].to<JsonObject>());
    JsonArray sw = o["switches"].to<JsonArray>();
    for (size_t i = 0; i < s.module->switchCount(); i++) {
        const SwitchDef* d = s.module->switchDef(i);
        JsonObject x = sw.add<JsonObject>();
        x["name"] = d->name;
        x["label"] = d->label;
        x["on"] = s.module->switchState(i);
    }
}

// mutex_는 모듈 포인터 수명(destroySlot)만 보호한다. 폴링 중 값 갱신과 겹치면 일부 필드가
// 직전 값일 수 있으나 모두 단일 워드 쓰기라 허용한다.
void Scheduler::snapshot(JsonDocument& doc) {
    JsonArray arr = doc.to<JsonArray>();
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE) return;
    for (uint8_t i = 0; i < MAX_SLOTS; i++) slotJson(slots_[i], arr.add<JsonObject>());
    xSemaphoreGive(mutex_);
}

bool Scheduler::switchState(uint8_t slot, const char* name, bool& out) {
    if (slot >= MAX_SLOTS) return false;
    const Slot& s = slots_[slot];
    if (!s.enabled || !s.module) return false;
    int idx = s.module->switchIndex(name);
    if (idx < 0) return false;
    out = s.module->switchState(idx);
    return true;
}

}  // namespace essio
