#pragma once
#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include "Config.h"

// WiFi STA / AP 폴백 / mDNS / 캡티브 DNS. docs/11-architecture.md §5.1
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

private:
    void startSta();
    void startAp();
    void stopAp();
    void startMdns();
    void onEvent(WiFiEvent_t event);

    ConfigStore* store_ = nullptr;
    NetState state_ = NetState::Boot;
    bool apActive_ = false;
    bool mdnsStarted_ = false;
    uint32_t staStartMs_ = 0;
    String hostname_;
    String apSsid_;
    DNSServer dns_;
};

}  // namespace essio
