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

void NetManager::applyConfig() {
    const Config& cfg = store_->get();
    hostname_ = cfg.device.hostname.length() ? cfg.device.hostname : "essio-" + deviceId();
    apSsid_ = cfg.wifi.ap.ssid.length() ? cfg.wifi.ap.ssid : "essio-" + deviceId();
    hostname_.toLowerCase();

    WiFi.disconnect(true, false);
    stopAp();
    mdnsStarted_ = false;
    MDNS.end();

    if (cfg.wifi.ssid.length() > 0) {
        startSta();
    } else {
        LOG_I("net: no SSID, AP only");
        WiFi.mode(WIFI_AP);
        startAp();
        state_ = NetState::ApOnly;
    }
}

void NetManager::startSta() {
    const Config& cfg = store_->get();
    WiFi.mode(apActive_ ? WIFI_AP_STA : WIFI_STA);
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
    LOG_I("net: connecting to %s", cfg.wifi.ssid.c_str());
    WiFi.begin(cfg.wifi.ssid.c_str(), cfg.wifi.password.c_str());
    staStartMs_ = millis();
    state_ = apActive_ ? NetState::ApSta : NetState::StaConnecting;
}

void NetManager::startAp() {
    if (apActive_) return;
    const Config& cfg = store_->get();
    WiFi.softAP(apSsid_.c_str(), cfg.wifi.ap.password.length() ? cfg.wifi.ap.password.c_str() : nullptr);
    dns_.setErrorReplyCode(DNSReplyCode::NoError);
    dns_.start(53, "*", WiFi.softAPIP());
    apActive_ = true;
    LOG_I("net: AP %s @ %s", apSsid_.c_str(), WiFi.softAPIP().toString().c_str());
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
        mdnsStarted_ = true;
        LOG_I("net: mDNS %s.local", hostname_.c_str());
    } else {
        LOG_W("net: mDNS start failed");
    }
}

void NetManager::onEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            LOG_I("net: connected, IP %s RSSI %d", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            state_ = apActive_ ? NetState::ApSta : NetState::StaConnected;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            if (state_ == NetState::StaConnected || state_ == NetState::ApSta) {
                LOG_W("net: disconnected");
            }
            if (state_ != NetState::ApOnly) {
                state_ = apActive_ ? NetState::ApSta : NetState::StaConnecting;
                staStartMs_ = millis();
                WiFi.reconnect();
            }
            break;
        default:
            break;
    }
}

void NetManager::tick() {
    if (apActive_) dns_.processNextRequest();

    const Config& cfg = store_->get();
    bool sta = staConnected();

    if (sta) {
        startMdns();
        if (apActive_ && !cfg.wifi.ap.keepWhenStaOk && state_ == NetState::ApSta) {
            stopAp();
            WiFi.mode(WIFI_STA);
            state_ = NetState::StaConnected;
        }
    } else if (state_ == NetState::StaConnecting) {
        // F-SYS-3: 접속 실패가 지속되면 AP 병행 기동
        if (millis() - staStartMs_ > (uint32_t)cfg.wifi.ap.fallbackAfterS * 1000UL) {
            LOG_W("net: STA timeout, starting AP fallback");
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
