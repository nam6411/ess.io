#include "Mach.h"

#include "../../core/Logger.h"

namespace essio {

bool Mach::begin(SerialPort& port, uint8_t slaveId, JsonVariantConst) {
    port_ = &port;
    slaveId_ = slaveId;
    frameLen_ = 0;
    lastByteMs_ = millis();
    strlcpy(lastError_, "protocol not implemented (sniffing)", sizeof(lastError_));
    return true;
}

void Mach::closeFrame() {
    if (!frameLen_) return;
    char* p = lastHex_;
    for (size_t i = 0; i < frameLen_; i++) p += sprintf(p, i ? " %02X" : "%02X", frame_[i]);
    frames_++;
    LOG_D("mach: rx %u bytes: %s", (unsigned)frameLen_, lastHex_);
    frameLen_ = 0;
}

// 요청 없이 수신 버퍼만 계속 비운다(Busy를 돌려 스케줄러가 곧바로 다시 부르게 함).
// 새 프레임이 있으면 REPORT_MS마다 한 번 Done → state 발행, SILENT_MS 동안 조용하면 Error.
PollResult Mach::pollStep() {
    Stream* s = port_ ? port_->stream() : nullptr;
    if (!s) return PollResult::Error;
    const uint32_t now = millis();
    while (s->available()) {
        if (frameLen_ >= FRAME_MAX) closeFrame();
        frame_[frameLen_++] = (uint8_t)s->read();
        rxBytes_++;
        lastByteMs_ = now;
    }
    if (frameLen_ && now - lastByteMs_ >= FRAME_GAP_MS) closeFrame();

    if (frames_ != reportedFrames_ && now - lastReportMs_ >= REPORT_MS) {
        reportedFrames_ = frames_;
        lastReportMs_ = now;
        return PollResult::Done;
    }
    if (now - lastByteMs_ >= SILENT_MS) {
        lastByteMs_ = now;  // 다음 오류는 다시 SILENT_MS 뒤
        strlcpy(lastError_, "no data (protocol not implemented)", sizeof(lastError_));
        return PollResult::Error;
    }
    return PollResult::Busy;
}

void Mach::toJson(JsonObject out) const {
    out["rx_bytes"] = rxBytes_;
    out["frames"] = frames_;
    out["last_hex"] = lastHex_;
}

}  // namespace essio
