#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Display 역할: 브로커에서 받은 토픽으로 장치 목록을 만든다. 화면(LVGL)과 무관한 순수 모델.
//
// 노드가 발행하는 토픽 (docs/15-roles.md §3.3, §4)
//   <prefix>/meta                  {type,label,switches:[{n,l}],metrics:[{l,u,p}]}  retain
//   <prefix>/state                 드라이버 toJson 결과                               retain
//   <prefix>/availability          online | offline                                  retain
//   <prefix>/switch/<name>/state   ON | OFF                                           retain
// <prefix>는 <root>/<node> 또는 (슬롯 2개 이상인 노드) <root>/<node>/<slug>.
namespace essio {

struct DisplaySwitch {
    String name;
    String label;
    bool on = false;
    bool known = false;  // 상태 토픽을 한 번이라도 받았나
};

struct DisplayMetric {
    String label;
    String unit;
    String path;  // state JSON 안의 점 경로 (예: "pv.in_w")
};

// 테두리 그래프 종류 (meta.ring). Soc는 ring이 없고 대표값이 %일 때의 대체 표시
enum class RingStyle : uint8_t { None, Soc, Battery, Flows };

struct DisplayDevice {
    static constexpr uint8_t MAX_SWITCHES = 8;
    static constexpr uint8_t MAX_METRICS = 5;

    String prefix;
    String type;
    String label;
    bool online = false;
    bool hasMeta = false;
    JsonDocument state;
    DisplaySwitch switches[MAX_SWITCHES];
    uint8_t switchCount = 0;
    DisplayMetric metrics[MAX_METRICS];
    uint8_t metricCount = 0;
    RingStyle ring = RingStyle::None;
    String ringPath[3];
    uint8_t ringCount = 0;
    uint32_t updatedMs = 0;

    // 화면 갱신 표시. layoutDirty = 스위치·지표 구성이 바뀜(다시 그려야 함), valueDirty = 값만 바뀜
    bool layoutDirty = true;
    bool valueDirty = true;

    // 지표 i의 현재 값 (없으면 false)
    bool metricValue(uint8_t i, float& out) const;
    bool valueAt(const String& path, float& out) const;  // state JSON 점 경로 값
    int switchIndex(const String& name) const;
};

class DeviceModel {
public:
    static constexpr uint8_t MAX_DEVICES = 12;

    void begin(const String& root) { root_ = root; count_ = 0; }
    // 구독할 토픽 필터 목록
    void filters(String* out, uint8_t& n) const;
    // 토픽 하나 반영. 모델이 바뀌면 true
    bool apply(const String& topic, const String& payload);

    uint8_t count() const { return count_; }
    DisplayDevice* device(uint8_t i) { return i < count_ ? &devices_[i] : nullptr; }
    uint8_t onlineCount() const;
    bool listChanged() { bool c = listChanged_; listChanged_ = false; return c; }

private:
    DisplayDevice* findOrAdd(const String& prefix);
    void applyMeta(DisplayDevice& d, const String& payload);
    void applyState(DisplayDevice& d, const String& payload);

    String root_ = "rv";
    DisplayDevice devices_[MAX_DEVICES];
    uint8_t count_ = 0;
    bool listChanged_ = false;
};

}  // namespace essio
