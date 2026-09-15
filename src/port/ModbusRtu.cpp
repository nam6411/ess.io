#include "ModbusRtu.h"

#include "Crc16.h"

namespace essio {

namespace {
constexpr uint32_t INTER_FRAME_IDLE_MS = 20;  // 3.5 char @9600 ≈ 4ms; 여유 있게
constexpr uint8_t BROADCAST = 0;
}

void ModbusRtu::attach(Stream* stream, int8_t dePin, uint16_t timeoutMs) {
    stream_ = stream;
    dePin_ = dePin;
    timeoutMs_ = timeoutMs;
    if (dePin_ >= 0) {
        pinMode(dePin_, OUTPUT);
        digitalWrite(dePin_, LOW);
    }
}

const char* ModbusRtu::resultName(MbResult r) {
    switch (r) {
        case MbResult::Ok: return "ok";
        case MbResult::Timeout: return "timeout";
        case MbResult::CrcError: return "crc";
        case MbResult::Exception: return "exception";
        case MbResult::BadResponse: return "bad_response";
        default: return "not_ready";
    }
}

void ModbusRtu::flushInput() {
    while (stream_->available()) stream_->read();
}

MbResult ModbusRtu::transact(uint8_t slave, uint8_t fc, const uint8_t* pdu, size_t pduLen,
                             size_t expectedLen, uint8_t* rsp, size_t& rspLen) {
    if (!stream_) return MbResult::NotReady;
    if (pduLen + 4 > MAX_FRAME) return MbResult::BadResponse;

    uint8_t frame[MAX_FRAME];
    frame[0] = slave;
    frame[1] = fc;
    memcpy(&frame[2], pdu, pduLen);
    uint16_t crc = crc16Modbus(frame, pduLen + 2);
    frame[pduLen + 2] = crc & 0xFF;
    frame[pduLen + 3] = crc >> 8;
    size_t txLen = pduLen + 4;

    flushInput();
    if (dePin_ >= 0) digitalWrite(dePin_, HIGH);
    stream_->write(frame, txLen);
    stream_->flush();
    if (dePin_ >= 0) digitalWrite(dePin_, LOW);

    rspLen = 0;
    if (slave == BROADCAST) return MbResult::Ok;

    uint32_t start = millis();
    uint32_t lastByte = start;
    size_t want = expectedLen;
    while (rspLen < want) {
        if (stream_->available()) {
            rsp[rspLen++] = (uint8_t)stream_->read();
            lastByte = millis();
            if (rspLen == 2 && (rsp[1] & 0x80)) want = 5;  // 예외 응답
            if (rspLen >= MAX_FRAME) break;
        } else {
            uint32_t now = millis();
            if (rspLen == 0 && now - start > timeoutMs_) return MbResult::Timeout;
            if (rspLen > 0 && now - lastByte > INTER_FRAME_IDLE_MS) break;
            if (now - start > timeoutMs_ + INTER_FRAME_IDLE_MS) break;
            delay(1);
        }
    }

    if (rspLen < 4) return MbResult::Timeout;
    uint16_t rxCrc = rsp[rspLen - 2] | (rsp[rspLen - 1] << 8);
    if (crc16Modbus(rsp, rspLen - 2) != rxCrc) return MbResult::CrcError;
    if (rsp[0] != slave) return MbResult::BadResponse;
    if (rsp[1] & 0x80) {
        lastException_ = rsp[2];
        return MbResult::Exception;
    }
    if (rsp[1] != fc || rspLen != expectedLen) return MbResult::BadResponse;
    return MbResult::Ok;
}

MbResult ModbusRtu::readBits(uint8_t fc, uint8_t slave, uint16_t addr, uint16_t count, uint8_t* out) {
    uint8_t pdu[4] = {(uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(count >> 8), (uint8_t)count};
    size_t bytes = (count + 7) / 8;
    size_t rspLen;
    MbResult r = transact(slave, fc, pdu, 4, 3 + bytes + 2, buf_, rspLen);
    if (r != MbResult::Ok) return r;
    if (buf_[2] != bytes) return MbResult::BadResponse;
    memcpy(out, &buf_[3], bytes);
    return MbResult::Ok;
}

MbResult ModbusRtu::readCoils(uint8_t slave, uint16_t addr, uint16_t count, uint8_t* out) {
    return readBits(0x01, slave, addr, count, out);
}

MbResult ModbusRtu::readDiscreteInputs(uint8_t slave, uint16_t addr, uint16_t count, uint8_t* out) {
    return readBits(0x02, slave, addr, count, out);
}

MbResult ModbusRtu::readRegisters(uint8_t fc, uint8_t slave, uint16_t addr, uint16_t count, uint16_t* out) {
    uint8_t pdu[4] = {(uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(count >> 8), (uint8_t)count};
    size_t rspLen;
    MbResult r = transact(slave, fc, pdu, 4, 3 + count * 2 + 2, buf_, rspLen);
    if (r != MbResult::Ok) return r;
    if (buf_[2] != count * 2) return MbResult::BadResponse;
    for (uint16_t i = 0; i < count; i++) out[i] = (buf_[3 + i * 2] << 8) | buf_[4 + i * 2];
    return MbResult::Ok;
}

MbResult ModbusRtu::readHoldingRegisters(uint8_t slave, uint16_t addr, uint16_t count, uint16_t* out) {
    return readRegisters(0x03, slave, addr, count, out);
}

MbResult ModbusRtu::readInputRegisters(uint8_t slave, uint16_t addr, uint16_t count, uint16_t* out) {
    return readRegisters(0x04, slave, addr, count, out);
}

MbResult ModbusRtu::writeSingleCoil(uint8_t slave, uint16_t addr, bool on) {
    uint8_t pdu[4] = {(uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(on ? 0xFF : 0x00), 0x00};
    size_t rspLen;
    MbResult r = transact(slave, 0x05, pdu, 4, 8, buf_, rspLen);
    if (r != MbResult::Ok) return r;
    return memcmp(&buf_[2], pdu, sizeof(pdu)) == 0 ? MbResult::Ok : MbResult::BadResponse;
}

MbResult ModbusRtu::writeSingleRegister(uint8_t slave, uint16_t addr, uint16_t value) {
    uint8_t pdu[4] = {(uint8_t)(addr >> 8), (uint8_t)addr, (uint8_t)(value >> 8), (uint8_t)value};
    size_t rspLen;
    MbResult r = transact(slave, 0x06, pdu, 4, 8, buf_, rspLen);
    if (r != MbResult::Ok) return r;
    return memcmp(&buf_[2], pdu, sizeof(pdu)) == 0 ? MbResult::Ok : MbResult::BadResponse;
}

MbResult ModbusRtu::writeMultipleRegisters(uint8_t slave, uint16_t addr, uint16_t count, const uint16_t* values) {
    if (count == 0 || count > 120) return MbResult::BadResponse;
    uint8_t pdu[5 + 240];
    pdu[0] = addr >> 8;
    pdu[1] = addr;
    pdu[2] = count >> 8;
    pdu[3] = count;
    pdu[4] = count * 2;
    for (uint16_t i = 0; i < count; i++) {
        pdu[5 + i * 2] = values[i] >> 8;
        pdu[6 + i * 2] = values[i];
    }
    size_t rspLen;
    MbResult r = transact(slave, 0x10, pdu, 5 + count * 2, 8, buf_, rspLen);
    if (r != MbResult::Ok) return r;
    // FC16 응답은 시작 주소와 기록한 레지스터 개수를 echo한다.
    return memcmp(&buf_[2], pdu, 4) == 0 ? MbResult::Ok : MbResult::BadResponse;
}

}  // namespace essio
