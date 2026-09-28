// pio test -e native
#include <string.h>
#include <unity.h>

#include "../../src/modules/antbms/AntFrame.h"
#include "../../src/modules/jbdbms/JbdFrame.h"
#include "../../src/port/Crc16.h"

using namespace essio;

void test_crc16_known_vector() {
    // 표준 예제: 01 03 00 00 00 01 → 와이어 84 0A (crc = 0x0A84)
    const uint8_t f1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX16(0x0A84, crc16Modbus(f1, sizeof(f1)));
    // UPower 블록 A 요청: 슬레이브 10, FC04, addr 0x3500, count 19
    const uint8_t f2[] = {0x0A, 0x04, 0x35, 0x00, 0x00, 0x13};
    TEST_ASSERT_EQUAL_HEX16(0x70BF, crc16Modbus(f2, sizeof(f2)));
}

void test_jbd_build_basic_request() {
    uint8_t out[16];
    size_t n = jbd::buildRequest(jbd::READ, jbd::CMD_BASIC, nullptr, 0, out);
    const uint8_t expected[] = {0xDD, 0xA5, 0x03, 0x00, 0xFF, 0xFD, 0x77};
    TEST_ASSERT_EQUAL(7, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 7);
}

void test_jbd_build_cells_request() {
    uint8_t out[16];
    jbd::buildRequest(jbd::READ, jbd::CMD_CELLS, nullptr, 0, out);
    const uint8_t expected[] = {0xDD, 0xA5, 0x04, 0x00, 0xFF, 0xFC, 0x77};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 7);
}

void test_jbd_build_mosfet_request() {
    // docs/04 §3.3: DD 5A E1 02 00 <VAL> FF <0x1D-VAL> 77
    uint8_t payload[] = {0x00, 0x02};
    uint8_t out[16];
    jbd::buildRequest(jbd::WRITE, jbd::CMD_MOSFET, payload, 2, out);
    const uint8_t expected[] = {0xDD, 0x5A, 0xE1, 0x02, 0x00, 0x02, 0xFF, 0x1B, 0x77};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 9);
}

void test_jbd_verify_roundtrip() {
    // 응답 프레임을 직접 조립해 검증기가 통과시키는지
    uint8_t f[16] = {0xDD, 0x03, 0x00, 0x02, 0x12, 0x34};
    uint16_t chk = jbd::checksum(f, 2, 6);
    f[6] = chk >> 8;
    f[7] = chk & 0xFF;
    f[8] = 0x77;
    uint8_t len = 0;
    TEST_ASSERT_EQUAL((int)jbd::Verify::Ok, (int)jbd::verifyResponse(f, 9, len));
    TEST_ASSERT_EQUAL(2, len);
    f[4] ^= 0xFF;
    TEST_ASSERT_EQUAL((int)jbd::Verify::BadChecksum, (int)jbd::verifyResponse(f, 9, len));
}

// ---- ANT BMS: 실측 프레임은 syssi/esphome-ant-bms tests/components 에서 가져왔다 ----

// 신형 16S 상태 프레임 (152바이트)
static const uint8_t ANT_NEW_16S[] = {0x7E, 0xA1, 0x11, 0x00, 0x00, 0x8E, 0x05, 0x01, 0x02, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE4, 0x0C, 0xE4, 0x0C, 0xE5, 0x0C, 0xE5, 0x0C, 0xE8, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C, 0xE6, 0x0C, 0xE8, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C, 0xE7, 0x0C, 0xE6, 0x0C, 0xE9, 0x0C, 0x01, 0x00, 0x02, 0x00, 0x02, 0x00, 0x07, 0x00, 0xA4, 0x14, 0x03, 0x00, 0x5B, 0x00, 0x64, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x76, 0xB0, 0x10, 0xD5, 0x67, 0x0E, 0x0F, 0xBA, 0x32, 0x4A, 0x00, 0x0F, 0x00, 0x00, 0x00, 0x10, 0x58, 0x2E, 0x02, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x0C, 0x10, 0x00, 0xE4, 0x0C, 0x01, 0x00, 0x05, 0x00, 0xE6, 0x0C, 0x00, 0x00, 0x80, 0x00, 0x7A, 0x00, 0x0F, 0x02, 0xF2, 0xFA, 0xB9, 0x8C, 0x3B, 0x00, 0xBB, 0xD8, 0x58, 0x00, 0xDA, 0x2D, 0x43, 0x00, 0xE8, 0xB6, 0x49, 0x00, 0x05, 0x43, 0xAA, 0x55,};

// 구형 8S 상태 프레임 (140바이트, 방전 중)
static const uint8_t ANT_OLD_8S[] = {0xAA, 0x55, 0xAA, 0xFF, 0x01, 0x0B, 0x0D, 0x0C, 0x0D, 0x0C, 0x0D, 0x0B, 0x0D, 0x0A, 0x0D, 0x0C, 0x0D, 0x0B, 0x0D, 0x0C, 0x0D, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x95, 0x29, 0x00, 0x00, 0x00, 0x00, 0x02, 0xE3, 0xDB, 0x95, 0x01, 0x26, 0x22, 0x63, 0x01, 0xB1, 0x6E, 0xBA, 0x00, 0x0F, 0x00, 0x0F, 0x00, 0x0D, 0x00, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFE, 0xE3, 0x01, 0x0D, 0x0C, 0x04, 0x0D, 0x0A, 0x0D, 0x0B, 0x08, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x0F, 0x0E, 0xDF,};

void test_ant_new_status_request() {
    uint8_t out[10];
    ant::buildNewStatusRequest(out);
    const uint8_t expected[] = {0x7E, 0xA1, 0x01, 0x00, 0x00, 0xBE, 0x18, 0x55, 0xAA, 0x55};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, 10);
}

void test_ant_new_parse_status() {
    ant::Status s;
    TEST_ASSERT_TRUE(ant::parseNewStatus(ANT_NEW_16S, sizeof(ANT_NEW_16S), s));
    TEST_ASSERT_EQUAL(16, s.cellCount);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 52.84f, s.packV);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.3f, s.current);
    TEST_ASSERT_EQUAL(91, s.soc);
    TEST_ASSERT_EQUAL(100, s.soh);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 280.0f, s.fullAh);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 252.602f, s.remainAh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.300f, s.cellV[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.305f, s.cellV[15]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.005f, s.cellDiff);
    TEST_ASSERT_EQUAL(2, s.tempCount);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, s.temp[0]);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, s.mosTemp);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, s.balTemp);
    TEST_ASSERT_EQUAL(1, s.chgStatus);  // On
    TEST_ASSERT_EQUAL(1, s.disStatus);
    TEST_ASSERT_EQUAL(36591632u, s.runtimeS);
}

void test_ant_new_rejects_corrupt() {
    uint8_t f[sizeof(ANT_NEW_16S)];
    memcpy(f, ANT_NEW_16S, sizeof(f));
    f[40] ^= 0x01;
    ant::Status s;
    TEST_ASSERT_EQUAL((int)ant::Verify::BadChecksum, (int)ant::verifyNew(f, sizeof(f)));
    TEST_ASSERT_FALSE(ant::parseNewStatus(f, sizeof(f), s));
}

void test_ant_new_auth_and_write() {
    uint8_t out[22];
    TEST_ASSERT_EQUAL(22, ant::buildNewAuth(out));
    TEST_ASSERT_EQUAL((int)ant::Verify::Ok, (int)ant::verifyNew(out, 22));  // 같은 CRC 규칙
    ant::buildNewWrite(ant::NEW_REG_CHARGE_ON, out);
    TEST_ASSERT_EQUAL_HEX8(0x51, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x06, out[3]);
    TEST_ASSERT_EQUAL((int)ant::Verify::Ok, (int)ant::verifyNew(out, 10));
}

void test_ant_old_requests() {
    uint8_t out[6];
    ant::buildOldStatusRequest(out);
    const uint8_t status[] = {0x5A, 0x5A, 0x00, 0x00, 0x01, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(status, out, 6);
    ant::buildOldSwitch(ant::OLD_REG_CHARGE, true, out);  // docs/09 §1: A5 A5 FA 00 01 FB
    const uint8_t chgOn[] = {0xA5, 0xA5, 0xFA, 0x00, 0x01, 0xFB};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(chgOn, out, 6);
    ant::buildOldSwitch(ant::OLD_REG_DISCHARGE, false, out);
    const uint8_t disOff[] = {0xA5, 0xA5, 0xF9, 0x00, 0x00, 0xF9};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(disOff, out, 6);
}

void test_ant_old_parse_status() {
    ant::Status s;
    TEST_ASSERT_TRUE(ant::parseOldStatus(ANT_OLD_8S, sizeof(ANT_OLD_8S), s));
    TEST_ASSERT_EQUAL(8, s.cellCount);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 26.7f, s.packV);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -10.7f, s.current);  // 방전 = 음수
    TEST_ASSERT_EQUAL(41, s.soc);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 48.487f, s.remainAh);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.340f, s.cellV[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.338f, s.cellV[3]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.002f, s.cellDiff);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, s.mosTemp);
    TEST_ASSERT_EQUAL(1, s.chgStatus);
    TEST_ASSERT_EQUAL(1, s.disStatus);
    TEST_ASSERT_EQUAL(28405434u, s.runtimeS);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_known_vector);
    RUN_TEST(test_jbd_build_basic_request);
    RUN_TEST(test_jbd_build_cells_request);
    RUN_TEST(test_jbd_build_mosfet_request);
    RUN_TEST(test_jbd_verify_roundtrip);
    RUN_TEST(test_ant_new_status_request);
    RUN_TEST(test_ant_new_parse_status);
    RUN_TEST(test_ant_new_rejects_corrupt);
    RUN_TEST(test_ant_new_auth_and_write);
    RUN_TEST(test_ant_old_requests);
    RUN_TEST(test_ant_old_parse_status);
    return UNITY_END();
}
