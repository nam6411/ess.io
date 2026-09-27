#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include "Config.h"

// WiFi STA / AP 폴백 / mDNS / 캡티브 DNS. docs/11-architecture.md §5.1, docs/15-roles.md
//
// 역할에 따라 동작이 다르다.
//   Broker — 부팅 시 라우터 SSID를 스캔해 있으면 STA 단독, 없으면 SoftAP 단독(설계서 §3.1).
//            APSTA 상시 동작을 피해 처리량·지연 저하를 막는다. STA가 끊기면 30초 주기로 재탐색.
//   Node   — 1순위 SSID(라우터) → 2순위 SSID(브로커 SoftAP) 순환 접속. 둘 다 실패가 계속되면
//            설정용 SoftAP를 함께 띄운다. Display도 Node와 같이 동작한다.
namespace essio {

enum class NetState : uint8_t { Boot, StaConnecting, StaConnected, ApSta, ApOnly };

class NetManager {
public:
    void begin(ConfigStore& store);
    void applyConfig();  // wifi.* 또는 device.hostname 변경 시
    void tick();

    NetState state() const { return state_; }
    const char* stateName() const;
    bool staConnected() const { return WiFi.status() == WL_CONNECTED; }
    bool apActive() const { return apActive_; }
    String staIp() const { return staConnected() ? WiFi.localIP().toString() : String(); }
    String apIp() const { return apActive_ ? WiFi.softAPIP().toString() : String(); }
    int rssi() const { return staConnected() ? WiFi.RSSI() : 0; }
    String hostname() const { return hostname_; }
    String apSsid() const { return apSsid_; }
    String connectedSsid() const { return staConnected() ? WiFi.SSID() : String(); }

private:
    static constexpr uint32_t ATTEMPT_TIMEOUT_MS = 15000;  // SSID 후보 하나당 시도 시간
    static constexpr uint32_t RESCAN_INTERVAL_MS = 30000;  // 설계서 §3.1 라우터 재탐색 주기

    void startSta(uint8_t candidate);
    void startAp();
    void stopAp();
    void startMdns();
    void onEvent(WiFiEvent_t event);
    bool routerVisible();  // 블로킹 스캔 (부팅 시 1회 + 재탐색)
    uint8_t candidateCount() const;
    const char* candidateSsid(uint8_t i) const;
    const char* candidatePassword(uint8_t i) const;

    ConfigStore* store_ = nullptr;
    NetState state_ = NetState::Boot;
    bool apActive_ = false;
    bool mdnsStarted_ = false;
    uint32_t attemptStartMs_ = 0;
    uint32_t staDownSinceMs_ = 0;
    uint32_t lastScanMs_ = 0;
    uint8_t candidate_ = 0;
    String hostname_;
    String apSsid_;
    DNSServer dns_;
};

}  // namespace essio
