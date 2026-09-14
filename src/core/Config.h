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

struct Config {
    struct {
        String name = "ESS Gateway";
        String hostname;
        String logLevel = "info";
    } device;

    struct {
        String ssid;
        String password;
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

    struct {
        bool enabled = true;
        String host;
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

private:
    bool parseInto(Config& out, JsonVariantConst src, String& error) const;
    Config cfg_;
    bool loadedFromFile_ = false;
};

String deviceId();  // 6자리 HEX (docs/01-legacy-system.md §4)

}  // namespace essio
