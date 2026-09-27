#pragma once
#include "../IDeviceModule.h"

// MACH BMS — 프로토콜 미확인. 지금은 자리만 잡아 둔 더미 드라이버다.
// 아무것도 보내지 않고 UART로 들어오는 바이트만 기록한다(수동 스니퍼).
// 실제 장치와 기존 컨트롤러 사이 선로에 RX만 물려 두면 패킷을 모을 수 있다.
// 모은 프레임은 state.last_hex와 로그(debug)로 확인한다.
namespace essio {

class Mach : public IDeviceModule {
public:
    static constexpr size_t FRAME_MAX = 64;       // 한 프레임으로 모을 최대 바이트
    static constexpr uint32_t FRAME_GAP_MS = 20;  // 이만큼 조용하면 프레임 끝으로 본다
    static constexpr uint32_t REPORT_MS = 1000;   // 새 프레임이 있어도 state 발행은 이 간격 이상
    static constexpr uint32_t SILENT_MS = 5000;   // 이만큼 아무것도 안 오면 오류(오프라인 판정용)

    const char* type() const override { return "mach"; }
    bool begin(SerialPort& port, uint8_t slaveId, JsonVariantConst params) override;
    PollResult pollStep() override;
    const char* lastError() const override { return lastError_; }
    void toJson(JsonObject out) const override;

private:
    void closeFrame();

    uint8_t frame_[FRAME_MAX];
    size_t frameLen_ = 0;
    uint32_t lastByteMs_ = 0;
    uint32_t lastReportMs_ = 0;
    uint32_t reportedFrames_ = 0;
    uint32_t rxBytes_ = 0;
    uint32_t frames_ = 0;
    char lastHex_[FRAME_MAX * 3 + 1] = {0};
    char lastError_[48] = {0};
};

}  // namespace essio
