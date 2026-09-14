#include "SerialPort.h"

#include "../core/Logger.h"

namespace essio {

bool SerialPort::begin(const PortConfig& cfg) {
    end();
    cfg_ = cfg;
    if (!mutex_) mutex_ = xSemaphoreCreateMutex();
    if (cfg.kind == PortKind::None) return false;

    switch (cfg.kind) {
        case PortKind::Hw1:
            hw_ = &Serial1;
            hw_->begin(cfg.baud, SERIAL_8N1, cfg.rx, cfg.tx);
            stream_ = hw_;
            break;
        case PortKind::Hw2:
            hw_ = &Serial2;
            hw_->begin(cfg.baud, SERIAL_8N1, cfg.rx, cfg.tx);
            stream_ = hw_;
            break;
        case PortKind::Sw:
            sw_ = new SoftwareSerial();
            sw_->begin(cfg.baud, SWSERIAL_8N1, cfg.rx, cfg.tx, false, 256);
            stream_ = sw_;
            break;
        default:
            return false;
    }
    stream_->setTimeout(cfg.timeoutMs);
    modbus_.attach(stream_, cfg.dePin, cfg.timeoutMs);
    LOG_I("port %u (%s): %s rx=%d tx=%d %lu bps", cfg.id, cfg.name.c_str(),
          ConfigStore::portKindName(cfg.kind), cfg.rx, cfg.tx, (unsigned long)cfg.baud);
    return true;
}

void SerialPort::end() {
    if (hw_) {
        hw_->end();
        hw_ = nullptr;
    }
    if (sw_) {
        sw_->end();
        delete sw_;
        sw_ = nullptr;
    }
    stream_ = nullptr;
    modbus_.attach(nullptr, -1, cfg_.timeoutMs);
}

bool SerialPort::lock(uint32_t waitMs) {
    if (!mutex_) return false;
    return xSemaphoreTake(mutex_, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

void SerialPort::unlock() {
    if (mutex_) xSemaphoreGive(mutex_);
}

}  // namespace essio
