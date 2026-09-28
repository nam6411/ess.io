#pragma once
#include <stddef.h>
#include <stdint.h>

#include "../../port/Crc16.h"

// ANT BMS 프레임 조립/해석. 순수 함수 — native 테스트 대상. docs/16-device-antbms.md
//
// 두 세대의 프로토콜이 있고 둘 다 UART 19200 8N1이다.
//   신형 (2021~, ANT-BMS 16S/24S 등): 7E A1 <func> <addr lo> <addr hi> <len> <data…> <crc lo> <crc hi> AA 55
//         CRC = Modbus CRC16, 바이트 1부터 len 끝까지. 값은 리틀 엔디언.
//   구형 (ANT-BLE16ZMUB 등): 요청 5A 5A 00 00 01 01 → 응답 140바이트 고정, 헤더 AA 55 AA FF,
//         체크섬 = 바이트 4..137 합(16비트, 빅 엔디언으로 138..139). 값은 빅 엔디언.
// 오프셋·배율 출처: syssi/esphome-ant-bms (ant_bms, ant_bms_old). 구형 요약은 docs/09 §1에도 있다.
namespace essio {
namespace ant {

constexpr uint8_t MAX_CELLS = 32;
constexpr uint8_t MAX_TEMPS = 6;

struct Status {
    float packV = 0;      // V
    float current = 0;    // A (부호: 장치 원값, invert는 드라이버에서)
    float fullAh = 0;     // 설정된 전체 용량
    float remainAh = 0;
    float cycleAh = 0;    // 누적 사이클 용량
    uint8_t soc = 0;      // %
    uint8_t soh = 0;      // % (신형만)
    uint8_t cellCount = 0;
    float cellV[MAX_CELLS] = {};
    float cellMax = 0, cellMin = 0, cellDiff = 0;
    uint8_t tempCount = 0;       // 셀 온도 센서 수
    float temp[MAX_TEMPS] = {};  // 셀 온도 °C
    float mosTemp = 0, balTemp = 0;
    uint8_t chgStatus = 0;       // 0 = Off, 1 = On, 그 밖 = 보호 사유 코드
    uint8_t disStatus = 0;
    uint8_t balStatus = 0;
    uint32_t runtimeS = 0;
};

// ---- 신형 ----

constexpr uint8_t NEW_HEAD1 = 0x7E, NEW_HEAD2 = 0xA1;
constexpr uint8_t NEW_FUNC_STATUS_REQ = 0x01, NEW_FUNC_STATUS = 0x11, NEW_FUNC_WRITE = 0x51, NEW_FUNC_AUTH = 0x23;
// 스위치 레지스터 (값 0으로 쓴다). 켜기/끄기 레지스터가 따로 있다
constexpr uint8_t NEW_REG_DISCHARGE_ON = 0x03, NEW_REG_DISCHARGE_OFF = 0x01;
constexpr uint8_t NEW_REG_CHARGE_ON = 0x06, NEW_REG_CHARGE_OFF = 0x04;

inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t le32(const uint8_t* p) { return (uint32_t)le16(p) | ((uint32_t)le16(p + 2) << 16); }
inline uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t be32(const uint8_t* p) { return ((uint32_t)be16(p) << 16) | be16(p + 2); }

// 10바이트 명령: 7E A1 <func> <b3> <b4> <b5> <crc lo> <crc hi> AA 55
inline size_t buildNew(uint8_t func, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t* out) {
    out[0] = NEW_HEAD1;
    out[1] = NEW_HEAD2;
    out[2] = func;
    out[3] = b3;
    out[4] = b4;
    out[5] = b5;
    const uint16_t crc = crc16Modbus(out + 1, 5);
    out[6] = crc & 0xFF;
    out[7] = crc >> 8;
    out[8] = 0xAA;
    out[9] = 0x55;
    return 10;
}

// 상태 요청: 7E A1 01 00 00 BE 18 55 AA 55
inline size_t buildNewStatusRequest(uint8_t* out) { return buildNew(NEW_FUNC_STATUS_REQ, 0x00, 0x00, 0xBE, out); }

// 레지스터 쓰기(스위치). 쓰기 전에 buildNewAuth 프레임을 먼저 보내야 한다
inline size_t buildNewWrite(uint8_t reg, uint8_t* out) { return buildNew(NEW_FUNC_WRITE, reg, 0x00, 0x00, out); }

// 쓰기 권한 인증: 기본 암호 "123456789abc" (22바이트)
inline size_t buildNewAuth(uint8_t* out) {
    const uint8_t head[] = {NEW_HEAD1, NEW_HEAD2, NEW_FUNC_AUTH, 0x6A, 0x01, 0x0C};
    const char* pw = "123456789abc";
    for (size_t i = 0; i < sizeof(head); i++) out[i] = head[i];
    for (size_t i = 0; i < 12; i++) out[6 + i] = (uint8_t)pw[i];
    const uint16_t crc = crc16Modbus(out + 1, 17);
    out[18] = crc & 0xFF;
    out[19] = crc >> 8;
    out[20] = 0xAA;
    out[21] = 0x55;
    return 22;
}

// 헤더 6바이트로 전체 프레임 길이 (= 6 + len + 4)
inline size_t newFrameLength(const uint8_t* head6) { return 6 + head6[5] + 4; }

enum class Verify : uint8_t { Ok, TooShort, BadHeader, BadLength, BadChecksum, BadFunction };

inline Verify verifyNew(const uint8_t* f, size_t n) {
    if (n < 10) return Verify::TooShort;
    if (f[0] != NEW_HEAD1 || f[1] != NEW_HEAD2) return Verify::BadHeader;
    const size_t len = newFrameLength(f);
    if (n != len || f[len - 2] != 0xAA || f[len - 1] != 0x55) return Verify::BadLength;
    const uint16_t crc = crc16Modbus(f + 1, len - 5);
    if ((uint16_t)(f[len - 4] | (f[len - 3] << 8)) != crc) return Verify::BadChecksum;
    return Verify::Ok;
}

// 신형 상태 프레임(func 0x11) 해석. 셀 수·온도 센서 수에 따라 뒤쪽 오프셋이 밀린다
inline bool parseNewStatus(const uint8_t* f, size_t n, Status& s) {
    if (verifyNew(f, n) != Verify::Ok || f[2] != NEW_FUNC_STATUS) return false;
    const uint8_t temps = f[8], cells = f[9];
    const size_t off = cells * 2 + temps * 2;
    if (cells > MAX_CELLS || 86 + off > n - 4) return false;  // 마지막 사용 필드(셀 편차, 84+off)까지 들어 있어야 한다
    s.cellCount = cells;
    for (uint8_t i = 0; i < cells; i++) s.cellV[i] = le16(f + 34 + i * 2) * 0.001f;
    s.tempCount = temps < MAX_TEMPS ? temps : MAX_TEMPS;
    for (uint8_t i = 0; i < s.tempCount; i++) s.temp[i] = (int16_t)le16(f + 34 + cells * 2 + i * 2);
    s.mosTemp = (int16_t)le16(f + 34 + off);
    s.balTemp = (int16_t)le16(f + 36 + off);
    s.packV = le16(f + 38 + off) * 0.01f;
    s.current = (int16_t)le16(f + 40 + off) * 0.1f;
    s.soc = (uint8_t)le16(f + 42 + off);
    s.soh = (uint8_t)le16(f + 44 + off);
    s.chgStatus = f[46 + off];
    s.disStatus = f[47 + off];
    s.balStatus = f[48 + off];
    s.fullAh = le32(f + 50 + off) * 0.000001f;
    s.remainAh = le32(f + 54 + off) * 0.000001f;
    s.cycleAh = le32(f + 58 + off) * 0.001f;
    s.runtimeS = le32(f + 66 + off);
    s.cellMax = le16(f + 74 + off) * 0.001f;
    s.cellMin = le16(f + 78 + off) * 0.001f;
    s.cellDiff = le16(f + 82 + off) * 0.001f;
    return true;
}

// ---- 구형 ----

constexpr size_t OLD_FRAME_LEN = 140;
constexpr uint8_t OLD_REG_DISCHARGE = 0xF9, OLD_REG_CHARGE = 0xFA;

// 6바이트 명령: <func> <func> <addr> <hi> <lo> <addr+hi+lo>. func 5A = 읽기, A5 = 쓰기
inline size_t buildOld(uint8_t func, uint8_t addr, uint16_t value, uint8_t* out) {
    out[0] = func;
    out[1] = func;
    out[2] = addr;
    out[3] = value >> 8;
    out[4] = value & 0xFF;
    out[5] = (uint8_t)(out[2] + out[3] + out[4]);
    return 6;
}

// 상태 요청: 5A 5A 00 00 01 01
inline size_t buildOldStatusRequest(uint8_t* out) { return buildOld(0x5A, 0x00, 0x0001, out); }

// MOSFET 켜기/끄기: A5 A5 FA 00 01 FB (충전 ON) 등
inline size_t buildOldSwitch(uint8_t reg, bool on, uint8_t* out) { return buildOld(0xA5, reg, on ? 1 : 0, out); }

inline uint16_t oldChecksum(const uint8_t* f) {
    uint16_t sum = 0;
    for (size_t i = 4; i < OLD_FRAME_LEN - 2; i++) sum += f[i];
    return sum;
}

inline Verify verifyOld(const uint8_t* f, size_t n) {
    if (n < OLD_FRAME_LEN) return Verify::TooShort;
    if (f[0] != 0xAA || f[1] != 0x55 || f[2] != 0xAA || f[3] != 0xFF) return Verify::BadHeader;
    if (be16(f + OLD_FRAME_LEN - 2) != oldChecksum(f)) return Verify::BadChecksum;
    return Verify::Ok;
}

inline bool parseOldStatus(const uint8_t* f, size_t n, Status& s) {
    if (verifyOld(f, n) != Verify::Ok) return false;
    const uint8_t cells = f[123] < MAX_CELLS ? f[123] : MAX_CELLS;
    s.packV = be16(f + 4) * 0.1f;
    s.cellCount = cells;
    for (uint8_t i = 0; i < cells; i++) s.cellV[i] = be16(f + 6 + i * 2) * 0.001f;
    s.current = (int32_t)be32(f + 70) * 0.1f;
    s.soc = f[74];
    s.fullAh = be32(f + 75) * 0.000001f;
    s.remainAh = be32(f + 79) * 0.000001f;
    s.cycleAh = be32(f + 83) * 0.001f;
    s.runtimeS = be32(f + 87);
    // 91: MOSFET, 93: 밸런서, 95~101: 셀 온도 4개
    s.mosTemp = (int16_t)be16(f + 91);
    s.balTemp = (int16_t)be16(f + 93);
    s.tempCount = 4;
    for (uint8_t i = 0; i < 4; i++) s.temp[i] = (int16_t)be16(f + 95 + i * 2);
    s.chgStatus = f[103];
    s.disStatus = f[104];
    s.balStatus = f[105];
    s.cellMax = be16(f + 116) * 0.001f;
    s.cellMin = be16(f + 119) * 0.001f;
    s.cellDiff = s.cellMax - s.cellMin;
    return true;
}

}  // namespace ant
}  // namespace essio
