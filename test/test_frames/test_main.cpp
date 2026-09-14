// pio test -e native
#include <unity.h>

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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc16_known_vector);
    RUN_TEST(test_jbd_build_basic_request);
    RUN_TEST(test_jbd_build_cells_request);
    RUN_TEST(test_jbd_build_mosfet_request);
    RUN_TEST(test_jbd_verify_roundtrip);
    return UNITY_END();
}
