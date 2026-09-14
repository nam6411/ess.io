#pragma once
#include <stddef.h>
#include <stdint.h>

// JBD 0xDD 프레임 조립/검증. 순수 함수 — native 테스트 대상. docs/04-device-jbdbms.md §2
namespace essio {
namespace jbd {

constexpr uint8_t START = 0xDD;
constexpr uint8_t END = 0x77;
constexpr uint8_t READ = 0xA5;
constexpr uint8_t WRITE = 0x5A;
constexpr uint8_t CMD_BASIC = 0x03;
constexpr uint8_t CMD_CELLS = 0x04;
constexpr uint8_t CMD_MOSFET = 0xE1;

// CHK = 0x10000 - Σ(bytes[from..to))
inline uint16_t checksum(const uint8_t* bytes, size_t from, size_t to) {
    uint32_t sum = 0;
    for (size_t i = from; i < to; i++) sum += bytes[i];
    return (uint16_t)(0x10000 - sum);
}

// 요청 프레임 조립. out 크기 >= 7 + dataLen. 반환: 프레임 길이
inline size_t buildRequest(uint8_t rw, uint8_t cmd, const uint8_t* data, uint8_t dataLen, uint8_t* out) {
    out[0] = START;
    out[1] = rw;
    out[2] = cmd;
    out[3] = dataLen;
    for (uint8_t i = 0; i < dataLen; i++) out[4 + i] = data[i];
    uint16_t chk = checksum(out, 2, 4 + dataLen);
    out[4 + dataLen] = chk >> 8;
    out[5 + dataLen] = chk & 0xFF;
    out[6 + dataLen] = END;
    return 7 + dataLen;
}

enum class Verify : uint8_t { Ok, TooShort, BadStart, BadEnd, StatusError, BadLength, BadChecksum };

// 응답 검증. 성공 시 dataLen에 DATA 길이(frame[3]) 반환.
inline Verify verifyResponse(const uint8_t* f, size_t len, uint8_t& dataLen) {
    if (len < 7) return Verify::TooShort;
    if (f[0] != START) return Verify::BadStart;
    if (f[len - 1] != END) return Verify::BadEnd;
    if (f[2] != 0x00) return Verify::StatusError;
    if (f[3] != len - 7) return Verify::BadLength;
    uint16_t chk = checksum(f, 2, len - 3);
    if (f[len - 3] != (chk >> 8) || f[len - 2] != (chk & 0xFF)) return Verify::BadChecksum;
    dataLen = f[3];
    return Verify::Ok;
}

inline uint16_t u16(const uint8_t* d) { return (uint16_t)(d[0] << 8 | d[1]); }
inline int16_t s16(const uint8_t* d) { return (int16_t)u16(d); }

}  // namespace jbd
}  // namespace essio
