#include "NetManager.h"

#include <ESPmDNS.h>

#include "Logger.h"

namespace essio {

void NetManager::begin(ConfigStore& store) {
    store_ = &store;
    WiFi.persistent(false);
    WiFi.setAutoReconnect(false);
    WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t) { onEvent(event); });
    applyConfig();
}

// 접속 후보: 라우터 → (노드·디스플레이만) 브로커 SoftAP.
// 브로커에게 fallback_ssid는 자기가 띄울 AP 이름이므로 후보가 아니다. 예전에는 후보로 넣어
// 라우터가 없는 브로커가 부팅 후 60초 동안 자기 AP 이름으로 접속을 시도하느라 AP를 늦게 띄웠다.
uint8_t NetManager::candidateCount() const {
    const auto& w = store_->get().wifi;
    uint8_t n = 0;
    if (w.ssid.length()) n++;
    if (w.fallbackSsid.length() && !store_->isBroker()) n++;
    return n;
}

const char* NetManager::candidateSsid(uint8_t i) const {
    const auto& w = store_->get().wifi;
    if (w.ssid.length()) {
        if (i == 0) return w.ssid.c_str();
        return w.fallbackSsid.c_str();
    }
    return w.fallbackSsid.c_str();
}

const char* NetManager::candidatePassword(uint8_t i) const {
    const auto& w = store_->get().wifi;
    if (w.ssid.length()) {
        if (i == 0) return w.password.c_str();
        return w.fallbackPassword.c_str();
    }
    return w.fallbackPassword.c_str();
}

void NetManager::applyConfig() {
    const Config& cfg = store_->get();
    const bool broker = cfg.device.role == DeviceRole::Broker;

    hostname_ = cfg.device.hostname.length() ? cfg.device.hostname
                : broker                                   ? String("broker")
                : cfg.device.role == DeviceRole::Display ? "rv-display-" + deviceId()
                                                           : "rv-node-" + deviceId();
    hostname_.toLowerCase();
    // 브로커의 SoftAP는 노드들이 2순위로 찾아오는 망이므로 이름을 고정한다(설계서 §3.2).
    apSsid_ = cfg.wifi.ap.ssid.length() ? cfg.wifi.ap.ssid
                                        : (broker ? (cfg.wifi.fallbackSsid.length() ? cfg.wifi.fallbackSsid
                                                                                    : String("RV-FALLBACK"))
                                                  : "RV-SETUP-" + deviceId());

    WiFi.disconnect(true, false);
    stopAp();
    mdnsStarted_ = false;
    MDNS.end();
    candidate_ = 0;
    lastScanMs_ = millis();

    if (candidateCount() == 0) {
        LOG_I("net: no SSID configured, AP only");
        WiFi.mode(WIFI_AP);
        startAp();
        state_ = NetState::ApOnly;
        return;
    }

    // 설계서 §3.1 — 브로커는 라우터가 보일 때만 STA로 간다. 안 보이면 바로 SoftAP 단독.
    if (broker && cfg.wifi.ssid.length() && !routerVisible()) {
        LOG_I("net: router '%s' not visible, SoftAP only", cfg.wifi.ssid.c_str());
        WiFi.mode(WIFI_AP);
        startAp();
        state_ = NetState::ApOnly;
        return;
    }

    startSta(0);
}

bool NetManager::routerVisible() {
    const String& target = store_->get().wifi.ssid;
    if (!target.length()) return false;
    lastScanMs_ = millis();
    // AP가 떠 있으면 스캔 동안만 APSTA로 둔다. WIFI_STA로 바꾸면 접속한 노드들이 끊긴다.
    // 설계서 §3.1도 "전환 중에만 APSTA를 짧게 유지"로 허용한다.
    WiFi.mode(apActive_ ? WIFI_AP_STA : WIFI_STA);
    // 채널당 120ms ≈ 1.6초. AP를 띄운 채 스캔하면 그동안 무선이 다른 채널에 가 있어
    // 접속한 노드가 비컨을 놓친다 — 길게(400ms ≈ 5초) 잡으면 노드들이 끊겼다.
    int n = WiFi.scanNetworks(false, false, false, 120);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == target) {
            found = true;
            LOG_I("net: router '%s' found (%d dBm)", target.c_str(), (int)WiFi.RSSI(i));
            break;
        }
    }
    WiFi.scanDelete();
    return found;
}

void NetManager::startSta(uint8_t candidate) {
    const Config& cfg = store_->get();
    candidate_ = candidate % (candidateCount() ? candidateCount() : 1);
    WiFi.mode(apActive_ ? WIFI_AP_STA : WIFI_STA);
    // 모뎀 슬립(기본값)이면 무선이 DTIM 사이에 잠들어 멀티캐스트(mDNS 질의·응답)를 놓친다.
    // 라우터에 붙은 디스플레이가 broker.local을 못 찾던 원인. 상시 전원 장비라 끈다.
    WiFi.setSleep(false);
    WiFi.setHostname(hostname_.c_str());
    if (cfg.wifi.staticIp.enabled) {
        IPAddress ip, gw, sn, dns;
        if (ip.fromString(cfg.wifi.staticIp.ip) && gw.fromString(cfg.wifi.staticIp.gateway) &&
            sn.fromString(cfg.wifi.staticIp.subnet)) {
            dns.fromString(cfg.wifi.staticIp.dns);
            WiFi.config(ip, gw, sn, dns);
        } else {
            LOG_W("net: invalid static IP config, using DHCP");
        }
    }
    LOG_I("net: connecting to %s", candidateSsid(candidate_));
    WiFi.begin(candidateSsid(candidate_), candidatePassword(candidate_));
    attemptStartMs_ = millis();
    if (!staDownSinceMs_) staDownSinceMs_ = millis();
    state_ = apActive_ ? NetState::ApSta : NetState::StaConnecting;
}

void NetManager::startAp() {
    if (apActive_) return;
    const Config& cfg = store_->get();
    uint8_t maxConn = cfg.broker.maxClients > 8 ? 8 : cfg.broker.maxClients;  // ESP32 softAP 상한
    WiFi.softAP(apSsid_.c_str(), cfg.wifi.ap.password.length() ? cfg.wifi.ap.password.c_str() : nullptr, 1, 0,
                maxConn);
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dns_.start(53, "*", WiFi.softAPIP());
    apActive_ = true;
    LOG_I("net: AP %s @ %s", apSsid_.c_str(), WiFi.softAPIP().toString().c_str());
    // 브로커는 자기 AP 위에서도 broker.local로 찾혀야 한다(노드의 2순위 망)
    if (store_->isBroker()) startMdns();
}

void NetManager::stopAp() {
    if (!apActive_) return;
    dns_.stop();
    WiFi.softAPdisconnect(true);
    apActive_ = false;
    LOG_I("net: AP stopped");
}

void NetManager::startMdns() {
    if (mdnsStarted_) return;
    if (MDNS.begin(hostname_.c_str())) {
        MDNS.addService("http", "tcp", 80);
        if (store_->isBroker()) MDNS.addService("mqtt", "tcp", store_->get().broker.port);
        mdnsStarted_ = true;
        LOG_I("net: mDNS %s.local", hostname_.c_str());
    } else {
        LOG_W("net: mDNS start failed");
    }
}

void NetManager::onEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG_I("net: connected to %s, IP %s RSSI %d", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
            staDownSinceMs_ = 0;
            state_ = apActive_ ? NetState::ApSta : NetState::StaConnected;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (state_ == NetState::StaConnected || state_ == NetState::ApSta) LOG_W("net: disconnected");
            if (state_ != NetState::ApOnly) {
                if (!staDownSinceMs_) staDownSinceMs_ = millis();
                state_ = apActive_ ? NetState::ApSta : NetState::StaConnecting;
            }
            break;
        default:
            break;
    }
}

void NetManager::tick() {
    if (apActive_) dns_.processNextRequest();

    const Config& cfg = store_->get();
    const bool broker = cfg.device.role == DeviceRole::Broker;
    const uint32_t now = millis();

    if (staConnected()) {
        startMdns();
        if (apActive_ && !cfg.wifi.ap.keepWhenStaOk && state_ == NetState::ApSta) {
            stopAp();
            WiFi.mode(WIFI_STA);
            state_ = NetState::StaConnected;
        }
        return;
    }

    if (state_ == NetState::ApOnly) {
        // 브로커: 라우터가 돌아왔는지 30초마다 확인해 STA로 승격 (설계서 §3.1)
        if (broker && cfg.wifi.ssid.length() && now - lastScanMs_ >= RESCAN_INTERVAL_MS) {
            if (routerVisible()) {
                LOG_I("net: router back, switching to STA");
                stopAp();
                startSta(0);
            } else {
                WiFi.mode(WIFI_AP);  // 스캔용 APSTA에서 AP 단독으로 복귀 (AP·DNS는 그대로 유지)
            }
        }
        return;
    }

    if (state_ != NetState::StaConnecting && state_ != NetState::ApSta) return;

    // 후보 SSID를 일정 시간마다 번갈아 시도 (노드: 라우터 → 브로커 SoftAP)
    if (now - attemptStartMs_ >= ATTEMPT_TIMEOUT_MS) {
        uint8_t n = candidateCount();
        if (n > 1) {
            LOG_W("net: '%s' timed out, trying next SSID", candidateSsid(candidate_));
            startSta(candidate_ + 1);
        } else {
            WiFi.reconnect();
            attemptStartMs_ = now;
        }
    }

    // 계속 못 붙으면 설정용 AP를 함께 띄운다. 브로커는 APSTA를 피해 AP 단독으로 내려간다.
    if (!apActive_ && staDownSinceMs_ && now - staDownSinceMs_ >= (uint32_t)cfg.wifi.ap.fallbackAfterS * 1000UL) {
        if (broker) {
            LOG_W("net: STA failed for %us, SoftAP only", cfg.wifi.ap.fallbackAfterS);
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_AP);
            startAp();
            state_ = NetState::ApOnly;
        } else {
            LOG_W("net: STA failed for %us, starting setup AP alongside", cfg.wifi.ap.fallbackAfterS);
            WiFi.mode(WIFI_AP_STA);
            startAp();
            state_ = NetState::ApSta;
        }
    }
}

const char* NetManager::stateName() const {
    switch (state_) {
        case NetState::StaConnecting: return "sta_connecting";
        case NetState::StaConnected: return "sta_connected";
        case NetState::ApSta: return "ap_sta";
        case NetState::ApOnly: return "ap_only";
        default: return "boot";
    }
}

}  // namespace essio
