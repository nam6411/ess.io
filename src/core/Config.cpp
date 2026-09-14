#include "Config.h"

#include <LittleFS.h>

#include "Logger.h"
#include "../modules/ModuleRegistry.h"

namespace essio {

namespace {

String getStr(JsonVariantConst v, const String& def) {
    const char* s = v.as<const char*>();
    return s ? String(s) : def;
}

template <typename T>
T getNum(JsonVariantConst v, T def) {
    return v.is<T>() ? v.as<T>() : def;
}

bool getBool(JsonVariantConst v, bool def) {
    return v.is<bool>() ? v.as<bool>() : def;
}

// 마스킹된 비밀번호는 기존 값 유지 (docs/12-config-schema.md §3)
void applySecret(JsonVariantConst v, String& target) {
    const char* s = v.as<const char*>();
    if (!s) return;
    if (strcmp(s, ConfigStore::MASK) == 0) return;
    target = s;
}

bool validSlug(const String& s) {
    if (s.length() < 1 || s.length() > 16) return false;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (!(isalnum(c) || c == '_') || isupper(c)) return false;
    }
    return true;
}

bool validGpio(int8_t pin, bool output) {
    if (pin < 0 || pin > 39) return false;
    if (output && pin >= 34) return false;  // 34~39 입력 전용
    return true;
}

void switchRefToJson(JsonObject o, const SwitchRef& r) {
    o["slot"] = r.slot;
    o["switch"] = r.name;
}

void switchRefFromJson(JsonVariantConst v, SwitchRef& r) {
    if (v.isNull()) { r = SwitchRef(); return; }
    r.slot = getNum<int8_t>(v["slot"], -1);
    r.name = getStr(v["switch"], "");
}

}  // namespace

String deviceId() {
    uint64_t mac = ESP.getEfuseMac();
    uint32_t id = 0;
    for (int i = 0; i < 17; i += 8) id |= ((mac >> (40 - i)) & 0xff) << i;
    char buf[8];
    snprintf(buf, sizeof(buf), "%06X", (unsigned)id);
    return String(buf);
}

const char* ConfigStore::portKindName(PortKind k) {
    switch (k) {
        case PortKind::Hw1: return "hw1";
        case PortKind::Hw2: return "hw2";
        case PortKind::Sw: return "sw";
        default: return "none";
    }
}

PortKind ConfigStore::parsePortKind(const String& s) {
    if (s == "hw1") return PortKind::Hw1;
    if (s == "hw2") return PortKind::Hw2;
    if (s == "sw") return PortKind::Sw;
    return PortKind::None;
}

bool ConfigStore::begin() {
    if (!LittleFS.begin(false)) {
        LOG_W("LittleFS mount failed, formatting");
        if (!LittleFS.begin(true)) {
            LOG_E("LittleFS format failed");
            setDefaults();
            return false;
        }
    }
    if (!load()) {
        LOG_W("config: using defaults");
        setDefaults();
    }
    return true;
}

// 기본값 = 기존 ess.io 구성 (docs/12-config-schema.md §1)
void ConfigStore::setDefaults() {
    cfg_ = Config();

    const struct { const char* name; PortKind kind; int8_t rx, tx; uint32_t baud; uint16_t timeout; } ports[MAX_PORTS] = {
        {"RS485-A", PortKind::Hw1, 22, 23, 115200, 500},
        {"BMS", PortKind::Hw2, 16, 17, 9600, 1000},
        {"RS485-B", PortKind::Sw, 18, 19, 9600, 500},
    };
    for (uint8_t i = 0; i < MAX_PORTS; i++) {
        PortConfig& p = cfg_.ports[i];
        p.id = i;
        p.name = ports[i].name;
        p.kind = ports[i].kind;
        p.rx = ports[i].rx;
        p.tx = ports[i].tx;
        p.baud = ports[i].baud;
        p.timeoutMs = ports[i].timeout;
    }

    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        cfg_.slots[i] = SlotConfig();
        cfg_.slots[i].index = i;
    }
    {
        SlotConfig& s = cfg_.slots[0];
        s.enabled = true; s.type = "upower"; s.slug = "upower"; s.label = "UPower";
        s.port = 0; s.slaveId = 10; s.pollIntervalMs = 5000;
        ModuleRegistry::defaultParams(s.type, s.params.to<JsonObject>());
    }
    {
        SlotConfig& s = cfg_.slots[1];
        s.enabled = true; s.type = "jbdbms"; s.slug = "bms"; s.label = "JBD BMS";
        s.port = 1; s.slaveId = 0; s.pollIntervalMs = 5000;
        ModuleRegistry::defaultParams(s.type, s.params.to<JsonObject>());
    }
    {
        SlotConfig& s = cfg_.slots[2];
        s.enabled = true; s.type = "rtusw_mk1"; s.slug = "relay"; s.label = "Relay Board";
        s.port = 2; s.slaveId = 255; s.pollIntervalMs = 3000;
        JsonObject params = s.params.to<JsonObject>();
        ModuleRegistry::defaultParams(s.type, params);
        const char* names[] = {"Equalizer", "Plumbing Drain", "Tank Drain", "Whale to Fill",
                               "Aroundview", "Mover", "12v Charger", "Channel 8"};
        JsonArray channels = params["channels"].to<JsonArray>();
        for (uint8_t ch = 1; ch <= 8; ch++) {
            JsonObject c = channels.add<JsonObject>();
            c["ch"] = ch;
            c["name"] = names[ch - 1];
            c["enabled"] = ch != 8;
        }
    }

    cfg_.buttonCount = 2;
    cfg_.buttons[0].pin = 14; cfg_.buttons[0].action = {2, "ch6"};
    cfg_.buttons[1].pin = 12; cfg_.buttons[1].action = {0, "inverter"};
    cfg_.outputCount = 2;
    cfg_.outputs[0].pin = 26; cfg_.outputs[0].source = {2, "ch6"};
    cfg_.outputs[1].pin = 25; cfg_.outputs[1].source = {0, "inverter"};

    loadedFromFile_ = false;
}

bool ConfigStore::load() {
    File f = LittleFS.open(PATH, "r");
    if (!f) {
        LOG_I("config: %s not found", PATH);
        return false;
    }
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        LOG_E("config: parse error %s", err.c_str());
        return false;
    }
    uint8_t version = doc["schema_version"] | 0;
    if (version != SCHEMA_VERSION) {
        // TODO: 마이그레이션 (docs/12-config-schema.md §5)
        LOG_W("config: schema_version %u != %u", version, SCHEMA_VERSION);
    }
    setDefaults();
    String error;
    if (!parseInto(cfg_, doc.as<JsonVariantConst>(), error)) {
        LOG_E("config: invalid: %s", error.c_str());
        return false;
    }
    loadedFromFile_ = true;
    LOG_I("config: loaded %s", PATH);
    return true;
}

bool ConfigStore::save() {
    JsonDocument doc;
    toJson(doc, false);
    File f = LittleFS.open(TMP_PATH, "w");
    if (!f) {
        LOG_E("config: open %s failed", TMP_PATH);
        return false;
    }
    size_t written = serializeJson(doc, f);
    f.close();
    if (written == 0) {
        LOG_E("config: write failed");
        LittleFS.remove(TMP_PATH);
        return false;
    }
    LittleFS.remove(PATH);
    if (!LittleFS.rename(TMP_PATH, PATH)) {
        LOG_E("config: rename failed");
        return false;
    }
    loadedFromFile_ = true;
    LOG_I("config: saved (%u bytes)", (unsigned)written);
    return true;
}

bool ConfigStore::remove() {
    return LittleFS.remove(PATH);
}

void ConfigStore::toJson(JsonDocument& doc, bool maskSecrets) const {
    auto secret = [&](const String& s) -> String {
        return (maskSecrets && s.length() > 0) ? String(MASK) : s;
    };

    doc["schema_version"] = SCHEMA_VERSION;

    JsonObject device = doc["device"].to<JsonObject>();
    device["name"] = cfg_.device.name;
    device["hostname"] = cfg_.device.hostname;
    device["log_level"] = cfg_.device.logLevel;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = cfg_.wifi.ssid;
    wifi["password"] = secret(cfg_.wifi.password);
    JsonObject st = wifi["static"].to<JsonObject>();
    st["enabled"] = cfg_.wifi.staticIp.enabled;
    st["ip"] = cfg_.wifi.staticIp.ip;
    st["gateway"] = cfg_.wifi.staticIp.gateway;
    st["subnet"] = cfg_.wifi.staticIp.subnet;
    st["dns"] = cfg_.wifi.staticIp.dns;
    JsonObject ap = wifi["ap"].to<JsonObject>();
    ap["ssid"] = cfg_.wifi.ap.ssid;
    ap["password"] = secret(cfg_.wifi.ap.password);
    ap["fallback_after_s"] = cfg_.wifi.ap.fallbackAfterS;
    ap["keep_when_sta_ok"] = cfg_.wifi.ap.keepWhenStaOk;

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["enabled"] = cfg_.mqtt.enabled;
    mqtt["host"] = cfg_.mqtt.host;
    mqtt["port"] = cfg_.mqtt.port;
    mqtt["username"] = cfg_.mqtt.username;
    mqtt["password"] = secret(cfg_.mqtt.password);
    mqtt["client_id_suffix"] = cfg_.mqtt.clientIdSuffix;
    mqtt["base_topic"] = cfg_.mqtt.baseTopic;
    mqtt["keepalive_s"] = cfg_.mqtt.keepaliveS;
    JsonObject disc = mqtt["discovery"].to<JsonObject>();
    disc["enabled"] = cfg_.mqtt.discoveryEnabled;
    disc["prefix"] = cfg_.mqtt.discoveryPrefix;
    mqtt["publish_min_interval_ms"] = cfg_.mqtt.publishMinIntervalMs;
    mqtt["legacy_topics"] = cfg_.mqtt.legacyTopics;

    JsonObject auth = doc["web"]["auth"].to<JsonObject>();
    auth["enabled"] = cfg_.webAuth.enabled;
    auth["username"] = cfg_.webAuth.username;
    auth["password"] = secret(cfg_.webAuth.password);

    JsonArray ports = doc["ports"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_PORTS; i++) {
        const PortConfig& p = cfg_.ports[i];
        JsonObject o = ports.add<JsonObject>();
        o["id"] = p.id;
        o["name"] = p.name;
        o["kind"] = portKindName(p.kind);
        o["rx"] = p.rx;
        o["tx"] = p.tx;
        o["baud"] = p.baud;
        o["de_pin"] = p.dePin;
        o["timeout_ms"] = p.timeoutMs;
    }

    JsonArray slots = doc["slots"].to<JsonArray>();
    for (uint8_t i = 0; i < MAX_SLOTS; i++) {
        const SlotConfig& s = cfg_.slots[i];
        JsonObject o = slots.add<JsonObject>();
        o["index"] = s.index;
        o["enabled"] = s.enabled;
        o["type"] = s.type;
        o["slug"] = s.slug;
        o["label"] = s.label;
        o["port"] = s.port;
        o["slave_id"] = s.slaveId;
        o["poll_interval_ms"] = s.pollIntervalMs;
        o["params"].set(s.params.as<JsonVariantConst>());
    }

    JsonObject io = doc["io"].to<JsonObject>();
    JsonArray buttons = io["buttons"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg_.buttonCount; i++) {
        const ButtonConfig& b = cfg_.buttons[i];
        JsonObject o = buttons.add<JsonObject>();
        o["pin"] = b.pin;
        o["active_low"] = b.activeLow;
        o["debounce_ms"] = b.debounceMs;
        switchRefToJson(o["action"].to<JsonObject>(), b.action);
        o["action"]["mode"] = b.mode;
        o["long_press_ms"] = b.longPressMs;
        if (b.longAction.valid()) switchRefToJson(o["long_action"].to<JsonObject>(), b.longAction);
        else o["long_action"] = nullptr;
    }
    JsonArray outputs = io["outputs"].to<JsonArray>();
    for (uint8_t i = 0; i < cfg_.outputCount; i++) {
        const OutputConfig& out = cfg_.outputs[i];
        JsonObject o = outputs.add<JsonObject>();
        o["pin"] = out.pin;
        o["active_high"] = out.activeHigh;
        switchRefToJson(o["source"].to<JsonObject>(), out.source);
    }
}

bool ConfigStore::parseInto(Config& c, JsonVariantConst src, String& error) const {
    if (!src.is<JsonObjectConst>()) { error = "root must be object"; return false; }

    JsonVariantConst device = src["device"];
    if (!device.isNull()) {
        c.device.name = getStr(device["name"], c.device.name);
        c.device.hostname = getStr(device["hostname"], c.device.hostname);
        c.device.logLevel = getStr(device["log_level"], c.device.logLevel);
        if (c.device.name.length() > 32) { error = "device.name too long"; return false; }
    }

    JsonVariantConst wifi = src["wifi"];
    if (!wifi.isNull()) {
        c.wifi.ssid = getStr(wifi["ssid"], c.wifi.ssid);
        applySecret(wifi["password"], c.wifi.password);
        JsonVariantConst st = wifi["static"];
        if (!st.isNull()) {
            c.wifi.staticIp.enabled = getBool(st["enabled"], c.wifi.staticIp.enabled);
            c.wifi.staticIp.ip = getStr(st["ip"], c.wifi.staticIp.ip);
            c.wifi.staticIp.gateway = getStr(st["gateway"], c.wifi.staticIp.gateway);
            c.wifi.staticIp.subnet = getStr(st["subnet"], c.wifi.staticIp.subnet);
            c.wifi.staticIp.dns = getStr(st["dns"], c.wifi.staticIp.dns);
        }
        JsonVariantConst ap = wifi["ap"];
        if (!ap.isNull()) {
            c.wifi.ap.ssid = getStr(ap["ssid"], c.wifi.ap.ssid);
            applySecret(ap["password"], c.wifi.ap.password);
            c.wifi.ap.fallbackAfterS = getNum<uint16_t>(ap["fallback_after_s"], c.wifi.ap.fallbackAfterS);
            c.wifi.ap.keepWhenStaOk = getBool(ap["keep_when_sta_ok"], c.wifi.ap.keepWhenStaOk);
        }
        if (c.wifi.ssid.length() > 32) { error = "wifi.ssid too long"; return false; }
        if (c.wifi.password.length() > 64) { error = "wifi.password too long"; return false; }
        if (c.wifi.ap.password.length() > 0 && (c.wifi.ap.password.length() < 8 || c.wifi.ap.password.length() > 63)) {
            error = "wifi.ap.password must be 8..63 chars"; return false;
        }
        if (c.wifi.ap.fallbackAfterS < 10 || c.wifi.ap.fallbackAfterS > 600) { error = "wifi.ap.fallback_after_s out of range"; return false; }
    }

    JsonVariantConst mqtt = src["mqtt"];
    if (!mqtt.isNull()) {
        c.mqtt.enabled = getBool(mqtt["enabled"], c.mqtt.enabled);
        c.mqtt.host = getStr(mqtt["host"], c.mqtt.host);
        c.mqtt.port = getNum<uint16_t>(mqtt["port"], c.mqtt.port);
        c.mqtt.username = getStr(mqtt["username"], c.mqtt.username);
        applySecret(mqtt["password"], c.mqtt.password);
        c.mqtt.clientIdSuffix = getStr(mqtt["client_id_suffix"], c.mqtt.clientIdSuffix);
        c.mqtt.baseTopic = getStr(mqtt["base_topic"], c.mqtt.baseTopic);
        c.mqtt.keepaliveS = getNum<uint16_t>(mqtt["keepalive_s"], c.mqtt.keepaliveS);
        JsonVariantConst disc = mqtt["discovery"];
        if (!disc.isNull()) {
            c.mqtt.discoveryEnabled = getBool(disc["enabled"], c.mqtt.discoveryEnabled);
            c.mqtt.discoveryPrefix = getStr(disc["prefix"], c.mqtt.discoveryPrefix);
        }
        c.mqtt.publishMinIntervalMs = getNum<uint32_t>(mqtt["publish_min_interval_ms"], c.mqtt.publishMinIntervalMs);
        c.mqtt.legacyTopics = getBool(mqtt["legacy_topics"], c.mqtt.legacyTopics);

        if (c.mqtt.port == 0) { error = "mqtt.port out of range"; return false; }
        if (c.mqtt.keepaliveS < 10 || c.mqtt.keepaliveS > 300) { error = "mqtt.keepalive_s out of range"; return false; }
        const String& bt = c.mqtt.baseTopic;
        if (bt.length() > 64 || bt.indexOf('#') >= 0 || bt.indexOf('+') >= 0 ||
            bt.startsWith("/") || bt.endsWith("/")) {
            error = "mqtt.base_topic invalid"; return false;
        }
        if (c.mqtt.discoveryPrefix.length() == 0 || c.mqtt.discoveryPrefix.length() > 32) { error = "mqtt.discovery.prefix invalid"; return false; }
    }

    JsonVariantConst auth = src["web"]["auth"];
    if (!auth.isNull()) {
        c.webAuth.enabled = getBool(auth["enabled"], c.webAuth.enabled);
        c.webAuth.username = getStr(auth["username"], c.webAuth.username);
        applySecret(auth["password"], c.webAuth.password);
        if (c.webAuth.enabled && c.webAuth.password.length() == 0) { error = "web.auth.password required"; return false; }
    }

    JsonArrayConst ports = src["ports"].as<JsonArrayConst>();
    if (!ports.isNull()) {
        for (JsonObjectConst o : ports) {
            uint8_t id = getNum<uint8_t>(o["id"], 255);
            if (id >= MAX_PORTS) { error = "ports[].id out of range"; return false; }
            PortConfig& p = c.ports[id];
            p.id = id;
            p.name = getStr(o["name"], p.name);
            p.kind = parsePortKind(getStr(o["kind"], portKindName(p.kind)));
            p.rx = getNum<int8_t>(o["rx"], p.rx);
            p.tx = getNum<int8_t>(o["tx"], p.tx);
            p.baud = getNum<uint32_t>(o["baud"], p.baud);
            p.dePin = getNum<int8_t>(o["de_pin"], p.dePin);
            p.timeoutMs = getNum<uint16_t>(o["timeout_ms"], p.timeoutMs);
        }
        bool hw1 = false, hw2 = false;
        for (uint8_t i = 0; i < MAX_PORTS; i++) {
            const PortConfig& p = c.ports[i];
            if (p.kind == PortKind::None) continue;
            if (p.kind == PortKind::Hw1) { if (hw1) { error = "ports: hw1 used twice"; return false; } hw1 = true; }
            if (p.kind == PortKind::Hw2) { if (hw2) { error = "ports: hw2 used twice"; return false; } hw2 = true; }
            if (!validGpio(p.rx, false) || !validGpio(p.tx, true)) { error = "ports[" + String(i) + "]: invalid rx/tx"; return false; }
            if (p.dePin >= 0 && !validGpio(p.dePin, true)) { error = "ports[" + String(i) + "]: invalid de_pin"; return false; }
            if (p.baud < 1200 || p.baud > 115200) { error = "ports[" + String(i) + "]: baud out of range"; return false; }
            if (p.timeoutMs < 100 || p.timeoutMs > 3000) { error = "ports[" + String(i) + "]: timeout_ms out of range"; return false; }
        }
    }

    JsonArrayConst slots = src["slots"].as<JsonArrayConst>();
    if (!slots.isNull()) {
        for (JsonObjectConst o : slots) {
            uint8_t index = getNum<uint8_t>(o["index"], 255);
            if (index >= MAX_SLOTS) { error = "slots[].index out of range"; return false; }
            SlotConfig& s = c.slots[index];
            s.index = index;
            s.enabled = getBool(o["enabled"], s.enabled);
            String type = getStr(o["type"], s.type);
            if (type != s.type) {
                s.type = type;
                s.params.clear();
                ModuleRegistry::defaultParams(type, s.params.to<JsonObject>());
            }
            s.slug = getStr(o["slug"], s.slug);
            s.label = getStr(o["label"], s.label);
            s.port = getNum<uint8_t>(o["port"], s.port);
            s.slaveId = getNum<uint8_t>(o["slave_id"], s.slaveId);
            s.pollIntervalMs = getNum<uint32_t>(o["poll_interval_ms"], s.pollIntervalMs);
            if (!o["params"].isNull()) {
                // TODO: 타입별 params 검증 (docs/12-config-schema.md §2)
                s.params.set(o["params"]);
            }
        }
        for (uint8_t i = 0; i < MAX_SLOTS; i++) {
            SlotConfig& s = c.slots[i];
            if (s.type != "none" && !ModuleRegistry::isKnown(s.type)) { error = "slots[" + String(i) + "]: unknown type"; return false; }
            if (s.type == "none") s.enabled = false;
            if (!s.enabled) continue;
            if (s.slug.length() == 0) s.slug = s.type + String(i);
            if (!validSlug(s.slug)) { error = "slots[" + String(i) + "]: invalid slug"; return false; }
            for (uint8_t j = 0; j < i; j++) {
                if (c.slots[j].enabled && c.slots[j].slug == s.slug) { error = "slots: duplicate slug " + s.slug; return false; }
            }
            if (s.port >= MAX_PORTS || c.ports[s.port].kind == PortKind::None) { error = "slots[" + String(i) + "]: port not configured"; return false; }
            if (s.pollIntervalMs < 1000 || s.pollIntervalMs > 600000) { error = "slots[" + String(i) + "]: poll_interval_ms out of range"; return false; }
            if (s.label.length() > 32) { error = "slots[" + String(i) + "]: label too long"; return false; }
        }
    }

    JsonVariantConst io = src["io"];
    if (!io.isNull()) {
        JsonArrayConst buttons = io["buttons"].as<JsonArrayConst>();
        if (!buttons.isNull()) {
            if (buttons.size() > MAX_BUTTONS) { error = "io.buttons: too many"; return false; }
            c.buttonCount = 0;
            for (JsonObjectConst o : buttons) {
                ButtonConfig& b = c.buttons[c.buttonCount++];
                b = ButtonConfig();
                b.pin = getNum<int8_t>(o["pin"], -1);
                b.activeLow = getBool(o["active_low"], true);
                b.debounceMs = getNum<uint16_t>(o["debounce_ms"], 50);
                switchRefFromJson(o["action"], b.action);
                b.mode = getStr(o["action"]["mode"], "toggle");
                b.longPressMs = getNum<uint16_t>(o["long_press_ms"], 0);
                switchRefFromJson(o["long_action"], b.longAction);
                if (!validGpio(b.pin, false)) { error = "io.buttons: invalid pin"; return false; }
                if (b.mode != "toggle" && b.mode != "on" && b.mode != "off") { error = "io.buttons: invalid mode"; return false; }
            }
        }
        JsonArrayConst outputs = io["outputs"].as<JsonArrayConst>();
        if (!outputs.isNull()) {
            if (outputs.size() > MAX_OUTPUTS) { error = "io.outputs: too many"; return false; }
            c.outputCount = 0;
            for (JsonObjectConst o : outputs) {
                OutputConfig& out = c.outputs[c.outputCount++];
                out = OutputConfig();
                out.pin = getNum<int8_t>(o["pin"], -1);
                out.activeHigh = getBool(o["active_high"], true);
                switchRefFromJson(o["source"], out.source);
                if (!validGpio(out.pin, true)) { error = "io.outputs: invalid pin"; return false; }
            }
        }
    }

    // 핀 중복 검사 (포트 rx/tx/de, 버튼, 출력)
    int8_t used[MAX_PORTS * 3 + MAX_BUTTONS + MAX_OUTPUTS];
    size_t n = 0;
    auto add = [&](int8_t pin, const char* what) -> bool {
        if (pin < 0) return true;
        for (size_t i = 0; i < n; i++) {
            if (used[i] == pin) { error = String("pin ") + pin + " used twice (" + what + ")"; return false; }
        }
        used[n++] = pin;
        return true;
    };
    for (uint8_t i = 0; i < MAX_PORTS; i++) {
        if (c.ports[i].kind == PortKind::None) continue;
        if (!add(c.ports[i].rx, "port rx") || !add(c.ports[i].tx, "port tx") || !add(c.ports[i].dePin, "port de")) return false;
    }
    for (uint8_t i = 0; i < c.buttonCount; i++) if (!add(c.buttons[i].pin, "button")) return false;
    for (uint8_t i = 0; i < c.outputCount; i++) if (!add(c.outputs[i].pin, "output")) return false;

    return true;
}

bool ConfigStore::validate(JsonVariantConst src, String& error) const {
    Config tmp = cfg_;
    return parseInto(tmp, src, error);
}

bool ConfigStore::fromJson(JsonVariantConst src, String& error, uint16_t& changed) {
    Config next = cfg_;
    if (!parseInto(next, src, error)) return false;

    JsonDocument before, after;
    toJson(before, false);
    cfg_ = next;
    toJson(after, false);

    changed = 0;
    const struct { const char* key; uint16_t bit; } sections[] = {
        {"device", CFG_DEVICE}, {"wifi", CFG_WIFI}, {"mqtt", CFG_MQTT}, {"web", CFG_WEB},
        {"ports", CFG_PORTS}, {"slots", CFG_SLOTS}, {"io", CFG_IO},
    };
    for (const auto& s : sections) {
        String a, b;
        serializeJson(before[s.key], a);
        serializeJson(after[s.key], b);
        if (a != b) changed |= s.bit;
    }
    return true;
}

}  // namespace essio
