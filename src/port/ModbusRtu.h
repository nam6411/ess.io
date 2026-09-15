#pragma once
#include <Arduino.h>

// 경량 Modbus RTU 마스터. 호출마다 슬레이브 주소 지정, 타임아웃 설정 가능.
// docs/11-architecture.md §10
namespace essio {

enum class MbResult : uint8_t {
    Ok = 0,
    Timeout,
    CrcError,
    Exception,     // 슬레이브 예외 응답 (lastException() 참조)
    BadResponse,   // 길이/기능코드 불일치
    NotReady,      // 스트림 없음
};

class ModbusRtu {
public:
    static constexpr size_t MAX_FRAME = 256;

    void attach(Stream* stream, int8_t dePin, uint16_t timeoutMs);
    void setTimeout(uint16_t ms) { timeoutMs_ = ms; }

    // FC01: out[]에 바이트 단위 비트맵 (LSB = 첫 코일). out 크기 >= (count+7)/8
    MbResult readCoils(uint8_t slave, uint16_t addr, uint16_t count, uint8_t* out);
    // FC02: out[]에 바이트 단위 비트맵 (LSB = 첫 discrete input).
    MbResult readDiscreteInputs(uint8_t slave, uint16_t addr, uint16_t count, uint8_t* out);
    // FC03 / FC04: out 크기 >= count
    MbResult readHoldingRegisters(uint8_t slave, uint16_t addr, uint16_t count, uint16_t* out);
    MbResult readInputRegisters(uint8_t slave, uint16_t addr, uint16_t count, uint16_t* out);
    // FC05: 0xFF00 / 0x0000
    MbResult writeSingleCoil(uint8_t slave, uint16_t addr, bool on);
    // FC06
    MbResult writeSingleRegister(uint8_t slave, uint16_t addr, uint16_t value);
    // FC16
    MbResult writeMultipleRegisters(uint8_t slave, uint16_t addr, uint16_t count, const uint16_t* values);

    uint8_t lastException() const { return lastException_; }
    static const char* resultName(MbResult r);

private:
    MbResult readBits(uint8_t fc, uint8_t slave, uint16_t addr, uint16_t count, uint8_t* out);
    MbResult readRegisters(uint8_t fc, uint8_t slave, uint16_t addr, uint16_t count, uint16_t* out);
    MbResult transact(uint8_t slave, uint8_t fc, const uint8_t* pdu, size_t pduLen,
                      size_t expectedLen, uint8_t* rsp, size_t& rspLen);
    void flushInput();

    Stream* stream_ = nullptr;
    int8_t dePin_ = -1;
    uint16_t timeoutMs_ = 500;
    uint8_t lastException_ = 0;
    uint8_t buf_[MAX_FRAME];
};

}  // namespace essio
