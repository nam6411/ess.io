#pragma once
#if ESSIO_DISPLAY
#include <Arduino.h>
#include <lvgl.h>

// Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display (ESP32-S3 N16R8, 480x480 원형 IPS)
//   패널   ST7701S, RGB565 병렬 + 초기화용 3선 SPI (CS16 SCK2 SDA1)
//   터치   CST8xx @0x15, I2C SDA38 SCL39
//   확장   PCF8574 @0x21 — P0 터치 RST, P2 터치 INT, P3 LCD 전원, P4 LCD RST, P5 엔코더 버튼(입력)
//   엔코더 A42 B4, 백라이트 PWM GPIO6
// 핀·타이밍·초기화 순서는 Elecrow 공식 예제(RotaryScreen_2_1.ino)를 그대로 따른다.
// 이 보드는 GPIO 대부분을 패널이 쓰므로 RS485 포트·슬롯을 만들면 안 된다(Display 역할은 만들지 않음).
namespace essio {

class CrowPanel21 {
public:
    static constexpr uint16_t WIDTH = 480;
    static constexpr uint16_t HEIGHT = 480;

    // 패널·터치·엔코더를 켜고 LVGL 디스플레이·입력 장치를 등록한다
    bool begin();
    void setBrightness(uint8_t percent);

    // 메인 루프에서: 엔코더 누적 회전량(딤 상태 확인 전 원값), 버튼 클릭 여부
    int32_t takeRotation();
    bool takeClick();
    // 마지막 터치 시각 (화면 끄기 판단용)
    uint32_t lastTouchMs() const { return lastTouchMs_; }
    // 진단: 누적 flush 시간(µs)·픽셀·호출 수를 돌려주고 0으로 되돌린다
    static void takeFlushStats(uint32_t& us, uint32_t& px, uint32_t& calls);
    // true면 다음 터치 한 번은 LVGL에 전달하지 않는다(어두운 화면을 깨우는 터치가 버튼을 누르지 않게)
    void swallowNextTouch() { swallow_ = true; }

private:
    void expanderWrite(uint8_t pin, bool high);
    bool expanderRead(uint8_t pin);
    void pollButton();
    static void flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px);
    static void readTouch(lv_indev_t* indev, lv_indev_data_t* data);
    static void encoderTask(void* arg);

    uint8_t expander_ = 0xFF;
    volatile int32_t rotation_ = 0;
    bool click_ = false;
    uint8_t btnStable_ = 1, btnRaw_ = 1;
    uint32_t btnChangedMs_ = 0, lastBtnPollMs_ = 0;
    uint32_t lastTouchMs_ = 0;
    bool swallow_ = false;
    bool swallowing_ = false;
};

}  // namespace essio
#endif
