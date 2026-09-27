#include "DeviceModel.h"

namespace essio {

namespace {

// "pv.in_w" 같은 점 경로로 JSON 값을 찾는다
JsonVariantConst lookup(JsonVariantConst v, const String& path) {
    int from = 0;
    while (from <= (int)path.length() && !v.isNull()) {
        int dot = path.indexOf('.', from);
        if (dot < 0) dot = path.length();
        v = v[path.substring(from, dot)];
        from = dot + 1;
    }
    return v;
}

bool endsWith(const String& s, const char* suffix, String& head) {
    size_t n = strlen(suffix);
    if (s.length() < n || !s.endsWith(suffix)) return false;
    head = s.substring(0, s.length() - n);
    return true;
}

}  // namespace

bool DisplayDevice::metricValue(uint8_t i, float& out) const {
    if (i >= metricCount) return false;
    JsonVariantConst v = lookup(state.as<JsonVariantConst>(), metrics[i].path);
    if (v.is<bool>()) { out = v.as<bool>() ? 1 : 0; return true; }
    if (!v.is<float>()) return false;
    out = v.as<float>();
    return true;
}

int DisplayDevice::switchIndex(const String& name) const {
    for (uint8_t i = 0; i < switchCount; i++) {
        if (switches[i].name == name) return i;
    }
    return -1;
}

void DeviceModel::filters(String* out, uint8_t& n) const {
    const char* suffixes[] = {"/meta", "/state", "/availability", "/switch/+/state"};
    n = 0;
    for (const char* sfx : suffixes) {
        out[n++] = root_ + "/+" + sfx;
        out[n++] = root_ + "/+/+" + sfx;
    }
}

uint8_t DeviceModel::onlineCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < count_; i++) n += devices_[i].online ? 1 : 0;
    return n;
}

DisplayDevice* DeviceModel::findOrAdd(const String& prefix) {
    for (uint8_t i = 0; i < count_; i++) {
        if (devices_[i].prefix == prefix) return &devices_[i];
    }
    if (count_ >= MAX_DEVICES) return nullptr;
    DisplayDevice& d = devices_[count_++];
    d.prefix = prefix;
    d.label = prefix.substring(prefix.lastIndexOf('/') + 1);
    listChanged_ = true;
    return &d;
}

bool DeviceModel::apply(const String& topic, const String& payload) {
    if (!topic.startsWith(root_ + "/")) return false;
    String head;

    // 스위치 상태: <prefix>/switch/<name>/state — "/state"보다 먼저 봐야 한다
    int sw = topic.indexOf("/switch/");
    if (sw > 0 && topic.endsWith("/state")) {
        String name = topic.substring(sw + 8, topic.length() - 6);
        DisplayDevice* d = findOrAdd(topic.substring(0, sw));
        if (!d) return false;
        int i = d->switchIndex(name);
        if (i < 0) {
            // meta가 아직 없으면(구버전 노드 포함) 상태 토픽으로 스위치를 만든다
            if (d->switchCount >= DisplayDevice::MAX_SWITCHES) return false;
            i = d->switchCount++;
            d->switches[i].name = name;
            d->switches[i].label = name;
            d->layoutDirty = true;
        }
        d->switches[i].on = payload == "ON";
        d->switches[i].known = true;
        d->valueDirty = true;
        return true;
    }
    if (endsWith(topic, "/meta", head)) {
        DisplayDevice* d = findOrAdd(head);
        if (!d) return false;
        applyMeta(*d, payload);
        return true;
    }
    if (endsWith(topic, "/availability", head)) {
        DisplayDevice* d = findOrAdd(head);
        if (!d) return false;
        d->online = payload == "online";
        d->valueDirty = true;
        listChanged_ = true;  // 개요 화면의 온라인 수
        return true;
    }
    if (endsWith(topic, "/state", head)) {
        DisplayDevice* d = findOrAdd(head);
        if (!d) return false;
        applyState(*d, payload);
        return true;
    }
    return false;
}

void DeviceModel::applyMeta(DisplayDevice& d, const String& payload) {
    JsonDocument doc;
    if (deserializeJson(doc, payload)) return;
    d.hasMeta = true;
    d.type = doc["type"] | "";
    d.label = doc["label"] | d.label.c_str();

    // 스위치 목록은 meta 기준으로 다시 만들되, 이미 받은 상태는 이름으로 이어받는다
    DisplaySwitch prev[DisplayDevice::MAX_SWITCHES];
    uint8_t prevCount = d.switchCount;
    for (uint8_t i = 0; i < prevCount; i++) prev[i] = d.switches[i];
    d.switchCount = 0;
    for (JsonObjectConst o : doc["switches"].as<JsonArrayConst>()) {
        if (d.switchCount >= DisplayDevice::MAX_SWITCHES) break;
        DisplaySwitch& s = d.switches[d.switchCount++];
        s = DisplaySwitch();
        s.name = o["n"] | "";
        s.label = o["l"] | s.name.c_str();
        for (uint8_t i = 0; i < prevCount; i++) {
            if (prev[i].name == s.name) { s.on = prev[i].on; s.known = prev[i].known; }
        }
    }
    d.metricCount = 0;
    for (JsonObjectConst o : doc["metrics"].as<JsonArrayConst>()) {
        if (d.metricCount >= DisplayDevice::MAX_METRICS) break;
        DisplayMetric& m = d.metrics[d.metricCount++];
        m.label = o["l"] | "";
        m.unit = o["u"] | "";
        m.path = o["p"] | "";
    }
    d.layoutDirty = true;
    listChanged_ = true;
}

void DeviceModel::applyState(DisplayDevice& d, const String& payload) {
    if (deserializeJson(d.state, payload)) return;
    d.updatedMs = millis();
    // meta 없는 장치: 최상위 숫자 값 몇 개를 지표로 쓴다
    if (!d.hasMeta && d.metricCount == 0) {
        for (JsonPairConst kv : d.state.as<JsonObjectConst>()) {
            if (d.metricCount >= DisplayDevice::MAX_METRICS) break;
            if (!kv.value().is<float>()) continue;
            DisplayMetric& m = d.metrics[d.metricCount++];
            m.label = kv.key().c_str();
            m.path = kv.key().c_str();
        }
        if (d.metricCount) d.layoutDirty = true;
    }
    d.valueDirty = true;
}

}  // namespace essio
