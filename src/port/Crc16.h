#pragma once
#include <stddef.h>
#include <stdint.h>

// Modbus RTU CRC-16 (poly 0xA001, init 0xFFFF). 순수 함수 — native 테스트 대상.
namespace essio {

inline uint16_t crc16Modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

}  // namespace essio
