#pragma once
#include <Arduino.h>
#include <SoftwareSerial.h>

#include "../core/Config.h"
#include "ModbusRtu.h"

// 물리 UART/RS485 포트. 슬롯 여러 개가 공유할 수 있으므로 뮤텍스 제공.
// docs/11-architecture.md §3.2
namespace essio {

class SerialPort {
public:
    bool begin(const PortConfig& cfg);
    void end();
    bool isActive() const { return stream_ != nullptr; }

    Stream* stream() { return stream_; }
    ModbusRtu& modbus() { return modbus_; }
    const PortConfig& config() const { return cfg_; }

    bool lock(uint32_t waitMs = 0);
    void unlock();

private:
    PortConfig cfg_;
    HardwareSerial* hw_ = nullptr;
    SoftwareSerial* sw_ = nullptr;
    Stream* stream_ = nullptr;
    ModbusRtu modbus_;
    SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace essio
