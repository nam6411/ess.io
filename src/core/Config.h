#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// 설정 데이터 모델. 스키마: docs/12-config-schema.md
namespace essio {

constexpr uint8_t SCHEMA_VERSION = 1;
constexpr uint8_t MAX_PORTS = 3;
constexpr uint8_t MAX_SLOTS = 4;
constexpr uint8_t MAX_BUTTONS = 4;
constexpr uint8_t MAX_OUTPUTS = 4;
constexpr uint8_t MAX_CHANNELS = 8;

enum class PortKind : uint8_t { None, Hw1, Hw2, Sw };

struct PortConfig {
    uint8_t id = 0;
    String name;
    PortKind kind = PortKind::None;
    int8_t rx = -1;
    int8_t tx = -1;
    uint32_t baud = 9600;
    int8_t dePin = -1;
    uint16_t timeoutMs = 500;
};

struct SlotConfig {
    uint8_t index = 0;
    bool enabled = false;
    String type = "none";
    String slug;
    String label;
    uint8_t port = 0;
    uint8_t slaveId = 1;
    uint32_t pollIntervalMs = 5000;
    JsonDocument params;  // 타입별 파라미터 (ModuleRegistry::defaultParams 참조)
};

struct SwitchRef {
    int8_t slot = -1;
    String name;
    bool valid() const { return slot >= 0 && name.length() > 0; }
};

struct ButtonConfig {
    int8_t pin = -1;
    bool activeLow = true;
    uint16_t debounceMs = 50;
    SwitchRef action;
    String mode = "toggle";  // toggle | on | off
    uint16_t longPressMs = 0;
    SwitchRef longAction;
};

struct OutputConfig {
    int8_t pin = -1;
    bool activeHigh = true;
    SwitchRef source;
};

// 이 보드가 무엇으로 동작하는가 (docs/15-roles.md)
//   Broker — 로컬 MQTT 호스트. 장치 폴링 없음.
//   Node   — 장치 1대를 물고 폴링해 브로커로 발행하는 클라이언트.
enum class DeviceRole : uint8_t { Node, Broker };

struct Config {
    struct {
        DeviceRole role = DeviceRole::Node;
        String name = "ESS Gateway";
        String hostname;
        String logLevel = "info";
    } device;

    struct {
        String ssid;
        String password;
        // 라우터가 없을 때 브로커가 띄우는 SoftAP (노드의 2순위 접속 대상)
        String fallbackSsid;
        String fallbackPassword;
        struct {
            bool enabled = false;
            String ip, gateway, subnet, dns;
        } staticIp;
        struct {
            String ssid;
            String password = "12341234";
            uint16_t fallbackAfterS = 60;
            bool keepWhenStaOk = false;
        } ap;
    } wifi;

    // Node 역할: 접속할 브로커
    struct {
        bool enabled = true;
        String host;
        String mdnsName = "broker";  // <mdnsName>.local 탐색. 비우면 수동 주소만 사용
        uint16_t port = 1883;
        String username;
        String password;
        String clientIdSuffix;
        String baseTopic;
        uint16_t keepaliveS = 30;
        bool discoveryEnabled = true;
        String discoveryPrefix = "homeassistant";
        uint32_t publishMinIntervalMs = 1000;
        bool legacyTopics = false;
    } mqtt;

    // Broker 역할: 내장 MQTT 호스트
    struct {
        uint16_t port = 1883;
        String username;  // 비우면 익명 허용
        String password;
        uint8_t maxClients = 8;
        uint16_t retainSlots = 48;  // 보관할 retained 토픽 수
    } broker;

    struct {
        bool enabled = false;
        String username = "admin";
        String password;
    } webAuth;

    PortConfig ports[MAX_PORTS];
    SlotConfig slots[MAX_SLOTS];
    ButtonConfig buttons[MAX_BUTTONS];
    uint8_t buttonCount = 0;
    OutputConfig outputs[MAX_OUTPUTS];
    uint8_t outputCount = 0;
};

// 변경된 섹션 비트마스크 (docs/12-config-schema.md §4)
enum ConfigSection : uint16_t {
    CFG_DEVICE = 1 << 0,
    CFG_WIFI = 1 << 1,
    CFG_MQTT = 1 << 2,
    CFG_WEB = 1 << 3,
    CFG_PORTS = 1 << 4,
    CFG_SLOTS = 1 << 5,
    CFG_IO = 1 << 6,
    CFG_BROKER = 1 << 7,
    CFG_ROLE = 1 << 8,  // 역할 전환 — 재부팅으로만 적용
    CFG_ALL = 0xFFFF,
};

class ConfigStore {
public:
    static constexpr const char* PATH = "/config.json";
    static constexpr const char* TMP_PATH = "/config.tmp";
    static constexpr const char* MASK = "********";

    // LittleFS 마운트 + 로드. 파일 없음/손상 시 기본값.
    bool begin();
    Config& get() { return cfg_; }
    const Config& get() const { return cfg_; }
    bool loadedFromFile() const { return loadedFromFile_; }

    bool load();
    bool save();
    bool remove();
    void setDefaults();

    void toJson(JsonDocument& doc, bool maskSecrets) const;
    // 검증만 (웹 태스크에서 호출 가능, cfg_ 불변)
    bool validate(JsonVariantConst src, String& error) const;
    // 검증 후 적용. 실패 시 cfg_ 불변, error에 사유. 반환: 변경된 섹션 비트
    bool fromJson(JsonVariantConst src, String& error, uint16_t& changed);

    static const char* portKindName(PortKind k);
    static PortKind parsePortKind(const String& s);
    static const char* roleName(DeviceRole r);
    static DeviceRole parseRole(const String& s);

    DeviceRole role() const { return cfg_.device.role; }
    bool isBroker() const { return cfg_.device.role == DeviceRole::Broker; }
    // 활성 슬롯 수 / 첫 활성 슬롯 (-1 = 없음)
    uint8_t enabledSlotCount() const;
    int8_t firstEnabledSlot() const;

private:
    bool parseInto(Config& out, JsonVariantConst src, String& error) const;
    Config cfg_;
    bool loadedFromFile_ = false;
};

String deviceId();  // 6자리 HEX (docs/01-legacy-system.md §4)

}  // namespace essio
