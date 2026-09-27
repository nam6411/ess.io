#if ESSIO_DISPLAY
#include "CrowPanel21.h"

#include <Adafruit_CST8XX.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include "../core/Logger.h"

namespace essio {

namespace {

constexpr uint8_t I2C_SDA = 38, I2C_SCL = 39;
constexpr uint8_t EXPANDER_ADDR = 0x21, TOUCH_ADDR = 0x15;
constexpr uint8_t P_TP_RST = 0, P_TP_INT = 2, P_LCD_POWER = 3, P_LCD_RST = 4, P_ENC_SW = 5;
constexpr uint8_t ENC_A = 42, ENC_B = 4;
constexpr uint8_t BACKLIGHT = 6;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30, BUTTON_POLL_MS = 10;

CrowPanel21* self = nullptr;
Adafruit_CST8XX touch;

Arduino_DataBus* initBus = new Arduino_SWSPI(GFX_NOT_DEFINED /* DC: 9-bit SPI */, 16 /* CS */, 2 /* SCK */,
                                             1 /* SDA */, GFX_NOT_DEFINED /* MISO */);

Arduino_ESP32RGBPanel* rgbPanel = new Arduino_ESP32RGBPanel(
    40 /* DE */, 7 /* VSYNC */, 15 /* HSYNC */, 41 /* PCLK */,
    46 /* R0 */, 3 /* R1 */, 8 /* R2 */, 18 /* R3 */, 17 /* R4 */,
    14 /* G0 */, 13 /* G1 */, 12 /* G2 */, 11 /* G3 */, 10 /* G4 */, 9 /* G5 */,
    5 /* B0 */, 45 /* B1 */, 48 /* B2 */, 47 /* B3 */, 21 /* B4 */,
    1 /* hsync polarity */, 10 /* front porch */, 4 /* pulse width */, 20 /* back porch */,
    1 /* vsync polarity */, 10 /* front porch */, 4 /* pulse width */, 20 /* back porch */,
    0 /* pclk active neg */, 12000000 /* pixel clock */, false /* native endian */,
    0 /* de idle high */, 0 /* pclk idle high */,
    480 * 20 /* bounce buffer: Wi-Fi가 PSRAM 대역을 쓸 때 화면이 밀리지 않게 */);

Arduino_RGB_Display* gfx = new Arduino_RGB_Display(480, 480, rgbPanel, 0 /* rotation */, true /* auto flush */,
                                                   initBus, GFX_NOT_DEFINED /* RST: PCF8574 P4 */,
                                                   st7701_type5_init_operations, sizeof(st7701_type5_init_operations));

}  // namespace

void CrowPanel21::expanderWrite(uint8_t pin, bool high) {
    if (high) expander_ |= (1 << pin);
    else expander_ &= ~(1 << pin);
    Wire.beginTransmission(EXPANDER_ADDR);
    Wire.write(expander_);
    Wire.endTransmission();
}

// PCF8574는 준양방향: 입력으로 쓸 핀은 1을 써 둔 상태에서 읽는다(expander_의 해당 비트는 항상 1)
bool CrowPanel21::expanderRead(uint8_t pin) {
    if (Wire.requestFrom(EXPANDER_ADDR, (uint8_t)1) != 1) return true;
    return Wire.read() & (1 << pin);
}

bool CrowPanel21::begin() {
    self = this;
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    // 공식 예제의 순서: 전원 → LCD 리셋 → 터치 리셋 → 터치 INT high
    expander_ = 0xFF;
    expanderWrite(P_LCD_POWER, true);
    delay(100);
    expanderWrite(P_LCD_RST, true);
    delay(100);
    expanderWrite(P_LCD_RST, false);
    delay(120);
    expanderWrite(P_LCD_RST, true);
    delay(120);
    expanderWrite(P_TP_RST, true);
    delay(100);
    expanderWrite(P_TP_RST, false);
    delay(120);
    expanderWrite(P_TP_RST, true);
    delay(120);
    expanderWrite(P_TP_INT, true);
    delay(120);

    if (!gfx->begin()) {
        LOG_E("display: panel init failed");
        return false;
    }
    // MADCTL BGR 비트 — 공식 예제와 같이 두고 R/B는 flush에서 맞바꾼다
    initBus->beginWrite();
    initBus->writeCommand(0x36);
    initBus->write(0x08);
    initBus->endWrite();
    gfx->fillScreen(0x0000);

    if (!touch.begin(&Wire, TOUCH_ADDR)) LOG_W("display: touch controller not found");

    pinMode(ENC_A, INPUT);
    pinMode(ENC_B, INPUT);
    xTaskCreatePinnedToCore(encoderTask, "enc", 2048, this, 1, nullptr, 0);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    // 부분 갱신용 버퍼 2장(각 1/4 화면)을 PSRAM에 둔다
    const size_t bufBytes = WIDTH * HEIGHT / 4 * sizeof(uint16_t);
    void* buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    void* buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        LOG_E("display: PSRAM draw buffer allocation failed");
        return false;
    }
    lv_display_t* disp = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp, flush);
    lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, readTouch);
    lv_indev_set_display(indev, disp);

    // 전원 비트를 내리는 것까지가 공식 예제의 초기화 끝이다
    expanderWrite(P_LCD_POWER, false);
    ledcAttach(BACKLIGHT, 5000, 8);
    LOG_I("display: CrowPanel 2.1 ready");
    return true;
}

void CrowPanel21::setBrightness(uint8_t percent) {
    ledcWrite(BACKLIGHT, (uint32_t)min<uint8_t>(percent, 100) * 255 / 100);
}

// 이 보드의 ST7701 결선은 RGB565의 R/B 필드가 뒤바뀌어 보인다(공식 예제와 같은 처리)
void CrowPanel21::flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    uint16_t* p = (uint16_t*)px;
    for (uint32_t i = 0, n = w * h; i < n; i++) {
        const uint16_t c = p[i];
        p[i] = (c & 0x07E0) | ((c & 0x001F) << 11) | ((c & 0xF800) >> 11);
    }
    gfx->draw16bitRGBBitmap(area->x1, area->y1, p, w, h);
    lv_display_flush_ready(disp);
}

void CrowPanel21::readTouch(lv_indev_t*, lv_indev_data_t* data) {
    static int16_t lastX = 0, lastY = 0;
    if (!touch.touched()) {
        self->swallowing_ = false;
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = lastX;
        data->point.y = lastY;
        return;
    }
    self->lastTouchMs_ = millis();
    if (self->swallow_) {  // 화면을 깨우는 터치 — 손을 뗄 때까지 무시
        self->swallow_ = false;
        self->swallowing_ = true;
    }
    if (self->swallowing_) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    CST_TS_Point pt = touch.getPoint(0);
    // 공식 예제의 보정: y가 20px 아래로 읽힌다. 손가락을 대고 있을 때의 잔떨림(±3px)은 무시.
    const int16_t x = constrain(pt.x, 0, WIDTH - 1);
    const int16_t y = constrain(pt.y - 20, 0, HEIGHT - 1);
    if (abs(x - lastX) >= 4 || abs(y - lastY) >= 4) {
        lastX = x;
        lastY = y;
    }
    data->point.x = lastX;
    data->point.y = lastY;
    data->state = LV_INDEV_STATE_PRESSED;
}

// A상 상승 에지마다 한 칸 (공식 예제와 같은 2ms 샘플링)
void CrowPanel21::encoderTask(void* arg) {
    CrowPanel21* p = (CrowPanel21*)arg;
    int prevA = digitalRead(ENC_A);
    for (;;) {
        int a = digitalRead(ENC_A);
        if (a != prevA && a == HIGH) p->rotation_ += digitalRead(ENC_B) != a ? -1 : 1;
        prevA = a;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

int32_t CrowPanel21::takeRotation() {
    int32_t r = rotation_;
    rotation_ -= r;
    return r;
}

void CrowPanel21::pollButton() {
    const uint32_t now = millis();
    if (now - lastBtnPollMs_ < BUTTON_POLL_MS) return;
    lastBtnPollMs_ = now;
    uint8_t raw = expanderRead(P_ENC_SW) ? 1 : 0;
    if (raw != btnRaw_) {
        btnRaw_ = raw;
        btnChangedMs_ = now;
    }
    if (raw != btnStable_ && now - btnChangedMs_ >= BUTTON_DEBOUNCE_MS) {
        btnStable_ = raw;
        if (raw == 1) click_ = true;  // 뗄 때 한 번 (active low)
    }
}

bool CrowPanel21::takeClick() {
    pollButton();
    bool c = click_;
    click_ = false;
    return c;
}

}  // namespace essio
#endif
