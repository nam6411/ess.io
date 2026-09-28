#include "DisplayService.h"

#include "../core/Logger.h"

#if !ESSIO_DISPLAY

namespace essio {

void DisplayService::begin(ConfigStore& store, MqttManager& mqtt, NetManager& net) {
    store_ = &store;
    mqtt_ = &mqtt;
    net_ = &net;
    LOG_E("display: this build has no display support (use the esp32s3 firmware)");
}
void DisplayService::subscribeAll() {}
bool DisplayService::onMessage(const String&, const String&) { return false; }
void DisplayService::applyConfig() {}
void DisplayService::tick() {}

}  // namespace essio

#else

#include <lvgl.h>

#include "CrowPanel21.h"
#include "DeviceModel.h"

namespace essio {

namespace {

// ---- 화면 구성 (480x480 원형: 모든 요소를 반지름 240 원 안에 둔다) ----
constexpr uint32_t C_BG = 0x000000, C_TEXT = 0xF4F4F5, C_MUTED = 0x8A8A93, C_ACCENT = 0x14B8A6,
                   C_ON = 0x0D9488, C_OFF = 0x3F3F46, C_BAD = 0xEF4444, C_WARN = 0xF59E0B;
constexpr uint8_t SECONDARY = 4;  // 대표값 아래 작은 지표 수

// 테두리 그래프 색: 배터리(SOC·충방전)와 전력 흐름(태양광·그리드·인버터)
constexpr uint32_t C_SOC = 0x14B8A6, C_PWR = 0xA78BFA, C_PV = 0xFACC15, C_GRID = 0x3B82F6, C_LOAD = 0xF97316,
                   C_TRACK = 0x26262B;
constexpr int32_t BATTERY_MAX_W = 3000;  // 충·방전 링이 가득 차는 전력
constexpr int32_t FLOW_MAX_W = 3000;     // 전력 흐름 링 한 칸이 가득 차는 전력

// 테두리 그래프는 점(5°마다 하나)을 둘러 그린다. lv_arc는 지름 460px에서 그리기 비용이 커
// 한 프레임에 1초 넘게 걸리고 결국 워치독이 걸렸다. 점은 작아서 싸고, 값이 바뀐 점만 다시 그린다.
constexpr int32_t RING_RADIUS = 223, DOT_SIZE = 11, DOT_STEP_DEG = 5;
constexpr uint8_t MAX_SEG_DOTS = 60;

struct RingSeg {
    lv_obj_t* dots[MAX_SEG_DOTS] = {};
    uint8_t n = 0;
    uint32_t color = 0;
    int32_t min = 0, max = 100;
    bool symmetric = false;  // 가운데에서 양쪽으로 (양수 = 시계 방향)
    int16_t lit = -1;        // 마지막으로 칠한 상태 (같으면 건너뜀)
    int16_t litSign = 0;
};

struct Ring {
    RingSeg seg[3];
    uint8_t n = 0;
    RingStyle style = RingStyle::None;
    bool shown = true;
};

struct DeviceTile {
    lv_obj_t* tile = nullptr;
    Ring ring;
    lv_obj_t* title = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* hero = nullptr;
    lv_obj_t* heroCap = nullptr;
    lv_obj_t* mVal[SECONDARY] = {};
    lv_obj_t* mCap[SECONDARY] = {};
    lv_obj_t* swBox = nullptr;
    lv_obj_t* swBtn[DisplayDevice::MAX_SWITCHES] = {};
    uint8_t swShown = 0;
};

CrowPanel21 panel;
DeviceModel model;
ConfigStore* cfgStore = nullptr;
MqttManager* mqttRef = nullptr;
NetManager* netRef = nullptr;

lv_obj_t* tileview = nullptr;
lv_obj_t* homeTile = nullptr;
lv_obj_t* homeStatus = nullptr;
lv_obj_t* homeSoc = nullptr;  // 배터리 SOC (링 왼쪽과 같은 색)
lv_obj_t* homePwr = nullptr;  // 충·방전 전력 (링 오른쪽과 같은 색)
lv_obj_t* homeFoot = nullptr;
lv_obj_t* homeGrid = nullptr;

// 홈 화면 바로가기: 이름으로 장치 전체에서 스위치를 찾아 묶는다 (display.shortcuts)
struct Shortcut {
    lv_obj_t* btn = nullptr;
    int8_t dev = -1;  // 묶인 장치·스위치 (못 찾으면 -1)
    int8_t sw = -1;
};
Shortcut shortcuts[MAX_SHORTCUTS];
uint8_t shortcutCount = 0;
lv_obj_t* dots = nullptr;
DeviceTile tiles[DeviceModel::MAX_DEVICES];  // 모델 인덱스 순
uint8_t tileCount = 0;
// 페이지 순서는 도착 순서가 아니라 BMS → 인버터 → 스위치 → 기타. devAt[pos] = 모델 인덱스
uint8_t devAt[DeviceModel::MAX_DEVICES];
uint8_t posOf[DeviceModel::MAX_DEVICES];
uint8_t page = 0;  // 0 = 홈, pos+1 = 장치 devAt[pos]
Ring homeRing;

uint32_t lastInputMs = 0;
uint32_t lastTouchSeen = 0;
uint32_t lastStatusMs = 0;
bool dimmed = false;

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, lv_align_t align, int32_t x, int32_t y) {
    lv_obj_t* l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, align, x, y);
    lv_label_set_text(l, "");
    return l;
}

void plain(lv_obj_t* o) {
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

// 몽세라 폰트에는 '°'가 없다
String unitText(const String& u) {
    String s = u;
    s.replace("°", "");
    return s;
}

String fmtValue(float v, const String& unit) {
    char buf[16];
    if (unit == "W" || unit == "%" || unit == "Wh" || fabsf(v) >= 100) snprintf(buf, sizeof(buf), "%.0f", v);
    else if (fabsf(v) >= 10) snprintf(buf, sizeof(buf), "%.1f", v);
    else snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

String metricText(const DisplayDevice& d, uint8_t i, bool withUnit) {
    float v;
    if (!d.metricValue(i, v)) return "-";
    String s = fmtValue(v, d.metrics[i].unit);
    if (withUnit && d.metrics[i].unit.length()) s += " " + unitText(d.metrics[i].unit);
    return s;
}

// ---- 테두리 그래프 ----
// 각도: 0° = 3시, 시계 방향 (LVGL과 같음). 점은 반지름 RING_RADIUS 원 위에 놓는다.

void buildSeg(RingSeg& g, lv_obj_t* parent, int32_t start, int32_t end, uint32_t color, int32_t min, int32_t max,
              bool symmetric) {
    g = RingSeg();
    g.color = color;
    g.min = min;
    g.max = max;
    g.symmetric = symmetric;
    if (end < start) end += 360;
    for (int32_t a = start; a <= end && g.n < MAX_SEG_DOTS; a += DOT_STEP_DEG) {
        const float rad = a * (float)M_PI / 180.0f;
        lv_obj_t* d = lv_obj_create(parent);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, lv_color_hex(C_TRACK), 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(d, LV_ALIGN_CENTER, (int32_t)lroundf(cosf(rad) * RING_RADIUS),
                     (int32_t)lroundf(sinf(rad) * RING_RADIUS));
        g.dots[g.n++] = d;
    }
}

// 값에 맞춰 점을 칠한다. 일반: 처음부터 비율만큼. 대칭: 가운데 점에서 양수는 뒤로, 음수는 앞으로.
void setSeg(RingSeg& g, bool has, float v) {
    int16_t lit = 0, sign = 0;
    if (has) {
        if (g.symmetric) {
            const float f = constrain(v / (float)g.max, -1.0f, 1.0f);
            lit = (int16_t)lroundf(fabsf(f) * (g.n / 2));
            sign = f < 0 ? -1 : 1;
        } else {
            const float f = constrain((v - g.min) / (float)(g.max - g.min), 0.0f, 1.0f);
            lit = (int16_t)lroundf(f * g.n);
        }
    }
    if (lit == g.lit && sign == g.litSign) return;
    g.lit = lit;
    g.litSign = sign;
    const int16_t mid = g.n / 2;
    for (uint8_t i = 0; i < g.n; i++) {
        bool on;
        if (g.symmetric) on = lit && (sign > 0 ? (i >= mid && i < mid + lit) : (i <= mid && i > mid - lit));
        else on = i < lit;
        lv_obj_set_style_bg_color(g.dots[i], lv_color_hex(on ? g.color : C_TRACK), 0);
    }
}

uint32_t ringColor(RingStyle style, uint8_t i) {
    if (style == RingStyle::Battery) return i == 0 ? C_SOC : C_PWR;
    if (style == RingStyle::Flows) return i == 0 ? C_PV : i == 1 ? C_GRID : C_LOAD;
    return C_SOC;
}

void buildRing(Ring& r, lv_obj_t* parent, RingStyle style) {
    r = Ring();
    r.style = style;
    switch (style) {
        case RingStyle::Soc:  // 대표값 %만 있는 장치: 아래가 트인 링
            buildSeg(r.seg[r.n++], parent, 135, 45, C_SOC, 0, 100, false);
            break;
        case RingStyle::Battery:
            // 왼쪽 반원 = SOC (아래에서 위로), 오른쪽 반원 = 충·방전량 (3시에서 충전은 아래로, 방전은 위로)
            buildSeg(r.seg[r.n++], parent, 100, 260, C_SOC, 0, 100, false);
            buildSeg(r.seg[r.n++], parent, 280, 80, C_PWR, -BATTERY_MAX_W, BATTERY_MAX_W, true);
            break;
        case RingStyle::Flows: {
            // 세 칸: 왼쪽 = 태양광, 오른쪽 위 = 그리드, 아래 = 인버터 출력. 각각 시계 방향으로 참
            const int32_t seg[3][2] = {{150, 250}, {270, 10}, {30, 130}};
            for (uint8_t i = 0; i < 3; i++)
                buildSeg(r.seg[r.n++], parent, seg[i][0], seg[i][1], ringColor(style, i), 0, FLOW_MAX_W, false);
            break;
        }
        default:
            break;
    }
}

RingStyle ringStyleOf(const DisplayDevice& d) {
    if (d.ring != RingStyle::None) return d.ring;
    return d.metricCount && d.metrics[0].unit == "%" ? RingStyle::Soc : RingStyle::None;
}

// 링 i칸에 그릴 값의 경로 (Soc 대체 표시는 대표값)
String ringPath(const DisplayDevice& d, const Ring& r, uint8_t i) {
    if (r.style == RingStyle::Soc) return d.metricCount ? d.metrics[0].path : String();
    return i < d.ringCount ? d.ringPath[i] : String();
}

void updateRing(Ring& r, const DisplayDevice* d) {
    for (uint8_t i = 0; i < r.n; i++) {
        float v = 0;
        const bool has = d && d->online && d->valueAt(ringPath(*d, r, i), v);
        setSeg(r.seg[i], has, v);
    }
}

// 상태가 바뀔 때만 플래그를 건드린다 (같은 값을 매초 다시 넣어도 화면 전체가 다시 그려졌다)
void showRing(Ring& r, bool show) {
    if (r.shown == show) return;
    r.shown = show;
    for (uint8_t i = 0; i < r.n; i++) {
        for (uint8_t k = 0; k < r.seg[i].n; k++) {
            if (show) lv_obj_remove_flag(r.seg[i].dots[k], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(r.seg[i].dots[k], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// 지표 경로가 링의 한 칸이면 그 색 (값 글자를 링과 같은 색으로 칠해 어느 칸인지 알게 한다)
bool ringColorFor(const DisplayDevice& d, const Ring& r, const String& path, uint32_t& color) {
    for (uint8_t i = 0; i < r.n; i++) {
        if (ringPath(d, r, i) == path) {
            color = ringColor(r.style, i);
            return true;
        }
    }
    return false;
}

// ---- 조작 ----

void showPage(uint8_t p, bool anim = true) {
    if (p > tileCount) p = tileCount;
    page = p;
    lv_tileview_set_tile_by_index(tileview, p, 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

// ---- 스위치 확인 창 ----
// 스위치는 누르자마자 보내지 않고 항상 확인을 받는다(인버터·펌프 등 오조작 방지).
// 노브를 누르거나 돌리면 취소, CONFIRM_TIMEOUT_MS 동안 입력이 없으면 저절로 닫힌다.
constexpr uint32_t CONFIRM_TIMEOUT_MS = 10000;

struct PendingSwitch {
    lv_obj_t* overlay = nullptr;
    uint8_t dev = 0, sw = 0;
    bool want = false;
    uint32_t openedMs = 0;
} pending;

void closeConfirm() {
    if (!pending.overlay) return;
    lv_obj_delete(pending.overlay);
    pending.overlay = nullptr;
}

void onConfirm(lv_event_t* e) {
    const bool ok = (uintptr_t)lv_event_get_user_data(e);
    if (ok) {
        DisplayDevice* d = model.device(pending.dev);
        if (d && pending.sw < d->switchCount) {
            const DisplaySwitch& s = d->switches[pending.sw];
            // 상태는 노드가 다시 발행하는 switch/<name>/state로 확정된다(여기서 먼저 바꾸지 않음)
            mqttRef->publish(d->prefix + "/switch/" + s.name + "/set", pending.want ? "ON" : "OFF");
            LOG_I("display: %s %s -> %s", d->label.c_str(), s.name.c_str(), pending.want ? "ON" : "OFF");
        }
    }
    closeConfirm();
}

void toggleSwitch(uint8_t dev, uint8_t sw) {
    DisplayDevice* d = model.device(dev);
    if (!d || sw >= d->switchCount) return;
    closeConfirm();
    pending.dev = dev;
    pending.sw = sw;
    pending.want = !d->switches[sw].on;
    pending.openedMs = millis();

    // 화면 전체를 덮는 반투명 막 (뒤의 버튼이 눌리지 않게 클릭을 먹는다)
    lv_obj_t* ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ov, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_80, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);
    pending.overlay = ov;

    lv_obj_t* box = lv_obj_create(ov);
    lv_obj_set_size(box, 330, 230);
    lv_obj_center(box);
    lv_obj_set_style_radius(box, 28, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x18181B), 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* name = label(box, &lv_font_montserrat_20, C_MUTED, LV_ALIGN_TOP_MID, 0, 8);
    lv_label_set_text(name, d->switches[sw].label.c_str());
    lv_obj_t* q = label(box, &lv_font_montserrat_28, pending.want ? C_ACCENT : C_WARN, LV_ALIGN_TOP_MID, 0, 44);
    lv_label_set_text(q, pending.want ? "Turn ON?" : "Turn OFF?");

    auto button = [&](const char* text, uint32_t color, bool ok, int32_t x) {
        lv_obj_t* b = lv_button_create(box);
        lv_obj_set_size(b, 130, 60);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x, -6);
        lv_obj_set_style_radius(b, 30, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
        lv_obj_add_event_cb(b, onConfirm, LV_EVENT_CLICKED, (void*)(uintptr_t)ok);
        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_label_set_text(l, text);
        lv_obj_center(l);
    };
    button("Cancel", C_OFF, false, -72);
    button(pending.want ? "ON" : "OFF", pending.want ? C_ON : 0xB45309, true, 72);
}

void onSwitch(lv_event_t* e) {
    const uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    toggleSwitch(tag >> 8, tag & 0xFF);
}

void onShortcut(lv_event_t* e) {
    const Shortcut& sc = shortcuts[(uintptr_t)lv_event_get_user_data(e)];
    if (sc.dev >= 0) toggleSwitch(sc.dev, sc.sw);
}

void onTileChanged(lv_event_t*) {
    lv_obj_t* active = lv_tileview_get_tile_active(tileview);
    page = active == homeTile ? 0 : page;
    for (uint8_t i = 0; i < tileCount; i++) {
        if (tiles[i].tile == active) page = posOf[i] + 1;
    }
}

// 페이지 순위: BMS → 인버터 → 스위치 → 기타
uint8_t pageRank(const DisplayDevice& d) {
    if (d.type == "jbdbms" || d.type == "mach" || d.type.indexOf("bms") >= 0) return 0;
    if (d.type == "upower") return 1;
    if (d.type.startsWith("rtusw")) return 2;
    return 3;
}

// 타일을 순위대로 다시 배치한다. 보고 있던 장치 페이지는 자리가 바뀌어도 그대로 보이게 한다
void arrangeTiles() {
    uint8_t order[DeviceModel::MAX_DEVICES];
    for (uint8_t i = 0; i < tileCount; i++) order[i] = i;
    for (uint8_t i = 1; i < tileCount; i++) {  // 안정 삽입 정렬 (같은 순위는 도착 순)
        uint8_t v = order[i];
        int j = i - 1;
        while (j >= 0 && pageRank(*model.device(order[j])) > pageRank(*model.device(v))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    bool same = true;
    for (uint8_t p = 0; p < tileCount; p++) same &= devAt[p] == order[p];
    if (same) return;
    const int16_t viewing = page > 0 ? devAt[page - 1] : -1;
    const int32_t w = lv_obj_get_width(tileview);
    for (uint8_t p = 0; p < tileCount; p++) {
        devAt[p] = order[p];
        posOf[order[p]] = p;
        lv_obj_set_pos(tiles[order[p]].tile, (p + 1) * w, 0);
    }
    showPage(viewing >= 0 ? posOf[viewing] + 1 : 0, false);
}

// ---- 장치 화면 ----

void buildDeviceTile(uint8_t i) {
    DisplayDevice& d = *model.device(i);
    DeviceTile& t = tiles[i];
    if (!t.tile) {
        // 일단 맨 뒤에 붙이고 arrangeTiles()가 순위 자리로 옮긴다
        t.tile = lv_tileview_add_tile(tileview, i + 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
        devAt[i] = i;
        posOf[i] = i;
    }
    lv_obj_clean(t.tile);
    t = DeviceTile{t.tile};

    buildRing(t.ring, t.tile, ringStyleOf(d));

    t.title = label(t.tile, &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_MID, 0, 58);
    lv_label_set_text(t.title, d.label.c_str());
    t.status = label(t.tile, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_MID, 0, 86);

    const bool hasMetrics = d.metricCount > 0;
    if (hasMetrics) {
        t.hero = label(t.tile, &lv_font_montserrat_48, C_TEXT, LV_ALIGN_TOP_MID, 0, 116);
        t.heroCap = label(t.tile, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_MID, 0, 172);
        lv_label_set_text(t.heroCap, d.metrics[0].label.c_str());
        // 나머지 지표: 2열 × 2행
        for (uint8_t m = 0; m < SECONDARY && m + 1 < d.metricCount; m++) {
            const int32_t x = (m % 2) ? 88 : -88;
            const int32_t y = 206 + (m / 2) * 52;
            t.mVal[m] = label(t.tile, &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_MID, x, y);
            t.mCap[m] = label(t.tile, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_MID, x, y + 24);
            lv_label_set_text(t.mCap[m], d.metrics[m + 1].label.c_str());
        }
    }

    // 스위치: 지표가 있으면 아래쪽 2열, 없으면(릴레이 보드) 가운데 3열
    t.swBox = lv_obj_create(t.tile);
    plain(t.swBox);
    lv_obj_set_flex_flow(t.swBox, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(t.swBox, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(t.swBox, 8, 0);
    lv_obj_set_style_pad_column(t.swBox, 8, 0);
    const uint8_t maxShown = hasMetrics ? 4 : DisplayDevice::MAX_SWITCHES;
    const int32_t bw = hasMetrics ? 150 : 116, bh = hasMetrics ? 40 : 52;
    if (hasMetrics) {
        lv_obj_set_size(t.swBox, 320, 96);
        lv_obj_align(t.swBox, LV_ALIGN_TOP_MID, 0, d.metricCount > 3 ? 318 : 266);
    } else {
        lv_obj_set_size(t.swBox, 380, 250);
        lv_obj_align(t.swBox, LV_ALIGN_TOP_MID, 0, 120);
    }
    t.swShown = min<uint8_t>(d.switchCount, maxShown);
    for (uint8_t s = 0; s < t.swShown; s++) {
        lv_obj_t* b = lv_button_create(t.swBox);
        lv_obj_set_size(b, bw, bh);
        lv_obj_set_style_radius(b, bh / 2, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_OFF), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_ON), LV_STATE_CHECKED);
        lv_obj_add_event_cb(b, onSwitch, LV_EVENT_CLICKED, (void*)(uintptr_t)((i << 8) | s));
        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, bw - 16);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(l, d.switches[s].label.c_str());
        lv_obj_center(l);
        t.swBtn[s] = b;
    }
    d.layoutDirty = false;
    d.valueDirty = true;
}

void updateDeviceTile(uint8_t i) {
    DisplayDevice& d = *model.device(i);
    DeviceTile& t = tiles[i];
    if (d.online) {
        lv_label_set_text(t.status, LV_SYMBOL_OK " online");
        lv_obj_set_style_text_color(t.status, lv_color_hex(C_ACCENT), 0);
    } else {
        lv_label_set_text(t.status, LV_SYMBOL_WARNING " offline");
        lv_obj_set_style_text_color(t.status, lv_color_hex(C_BAD), 0);
    }
    // 값 글자색: 오프라인이면 흐리게, 링의 한 칸이면 그 칸 색
    auto valueColor = [&](uint8_t metric) {
        uint32_t c = C_TEXT;
        if (!d.online) return (uint32_t)C_MUTED;
        ringColorFor(d, t.ring, d.metrics[metric].path, c);
        return c;
    };
    if (t.hero) {
        lv_label_set_text(t.hero, metricText(d, 0, true).c_str());
        lv_obj_set_style_text_color(t.hero, lv_color_hex(valueColor(0)), 0);
    }
    for (uint8_t m = 0; m < SECONDARY; m++) {
        if (!t.mVal[m]) continue;
        lv_label_set_text(t.mVal[m], metricText(d, m + 1, true).c_str());
        lv_obj_set_style_text_color(t.mVal[m], lv_color_hex(valueColor(m + 1)), 0);
    }
    updateRing(t.ring, &d);
    for (uint8_t s = 0; s < t.swShown; s++) {
        if (d.switches[s].on) lv_obj_add_state(t.swBtn[s], LV_STATE_CHECKED);
        else lv_obj_remove_state(t.swBtn[s], LV_STATE_CHECKED);
        if (d.online) lv_obj_remove_state(t.swBtn[s], LV_STATE_DISABLED);
        else lv_obj_add_state(t.swBtn[s], LV_STATE_DISABLED);
    }
    d.valueDirty = false;
}

// ---- 홈 화면 ----

void buildHome() {
    homeTile = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    buildRing(homeRing, homeTile, RingStyle::Battery);  // 배터리 장치(BMS)의 SOC·충방전량
    showRing(homeRing, false);
    lv_obj_t* title = label(homeTile, &lv_font_montserrat_28, C_TEXT, LV_ALIGN_TOP_MID, 0, 42);
    lv_label_set_text(title, "573PT");
    homeStatus = label(homeTile, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_MID, 0, 78);
    lv_obj_set_style_text_align(homeStatus, LV_TEXT_ALIGN_CENTER, 0);
    // 배터리 요약: SOC와 충·방전 전력 두 숫자만 (인버터·BMS가 같은 배터리라 장치 이름은 뺀다)
    lv_obj_t* summary = lv_obj_create(homeTile);
    plain(summary);
    lv_obj_set_size(summary, 360, 44);
    lv_obj_align(summary, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_set_flex_flow(summary, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(summary, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(summary, 28, 0);
    homeSoc = label(summary, &lv_font_montserrat_28, C_SOC, LV_ALIGN_DEFAULT, 0, 0);
    homePwr = label(summary, &lv_font_montserrat_28, C_PWR, LV_ALIGN_DEFAULT, 0, 0);

    // 바로가기 3열 × 2행
    homeGrid = lv_obj_create(homeTile);
    plain(homeGrid);
    lv_obj_set_size(homeGrid, 392, 136);
    lv_obj_align(homeGrid, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_flex_flow(homeGrid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(homeGrid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(homeGrid, 10, 0);
    lv_obj_set_style_pad_column(homeGrid, 10, 0);

    homeFoot = label(homeTile, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_BOTTOM_MID, 0, -66);
    lv_obj_set_style_text_align(homeFoot, LV_TEXT_ALIGN_CENTER, 0);
}

void buildShortcuts() {
    const auto& dc = cfgStore->get().display;
    lv_obj_clean(homeGrid);
    shortcutCount = dc.shortcutCount;
    for (uint8_t i = 0; i < shortcutCount; i++) {
        Shortcut& sc = shortcuts[i];
        sc = Shortcut();
        sc.btn = lv_button_create(homeGrid);
        lv_obj_set_size(sc.btn, 120, 60);
        lv_obj_set_style_radius(sc.btn, 16, 0);
        lv_obj_set_style_bg_color(sc.btn, lv_color_hex(C_OFF), 0);
        lv_obj_set_style_bg_color(sc.btn, lv_color_hex(C_ON), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sc.btn, lv_color_hex(0x18181B), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(sc.btn, lv_color_hex(0x52525B), LV_STATE_DISABLED);
        lv_obj_add_event_cb(sc.btn, onShortcut, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
        lv_obj_t* l = lv_label_create(sc.btn);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_label_set_text(l, dc.shortcuts[i].c_str());
        lv_obj_center(l);
    }
}

// 이름이 같은 스위치(표시명 또는 토픽 이름)를 장치 순서대로 찾아 묶고 상태를 반영한다
void updateShortcuts() {
    const auto& dc = cfgStore->get().display;
    for (uint8_t i = 0; i < shortcutCount; i++) {
        Shortcut& sc = shortcuts[i];
        sc.dev = sc.sw = -1;
        for (uint8_t d = 0; d < model.count() && sc.dev < 0; d++) {
            const DisplayDevice& dev = *model.device(d);
            for (uint8_t w = 0; w < dev.switchCount; w++) {
                if (dev.switches[w].label.equalsIgnoreCase(dc.shortcuts[i]) ||
                    dev.switches[w].name.equalsIgnoreCase(dc.shortcuts[i])) {
                    sc.dev = d;
                    sc.sw = w;
                    break;
                }
            }
        }
        const DisplayDevice* dev = sc.dev >= 0 ? model.device(sc.dev) : nullptr;
        const bool usable = dev && dev->online;
        if (usable) lv_obj_remove_state(sc.btn, LV_STATE_DISABLED);
        else lv_obj_add_state(sc.btn, LV_STATE_DISABLED);
        if (dev && dev->switches[sc.sw].on) lv_obj_add_state(sc.btn, LV_STATE_CHECKED);
        else lv_obj_remove_state(sc.btn, LV_STATE_CHECKED);
    }
}

void updateHome() {
    const Config& cfg = cfgStore->get();
    String st;
    if (netRef->staConnected()) st = String(LV_SYMBOL_WIFI " ") + netRef->connectedSsid();
    else if (netRef->apActive()) st = String(LV_SYMBOL_WIFI " setup: ") + netRef->apSsid() + "  " + netRef->apIp();
    else if (!cfg.wifi.ssid.length() && !cfg.wifi.fallbackSsid.length()) st = LV_SYMBOL_WIFI " not configured";
    else st = LV_SYMBOL_WIFI " connecting...";
    st += mqttRef->connected() ? "   MQTT " LV_SYMBOL_OK : "   MQTT " LV_SYMBOL_CLOSE;
    lv_label_set_text(homeStatus, st.c_str());
    lv_obj_set_style_text_color(homeStatus, lv_color_hex(mqttRef->connected() ? C_MUTED : C_WARN), 0);

    // 배터리 = 첫 번째 배터리 링 장치(BMS). 요약 숫자와 테두리 모두 그 ring 경로(soc, 전력)를 쓴다
    const DisplayDevice* battery = nullptr;
    for (uint8_t i = 0; i < model.count() && !battery; i++) {
        if (model.device(i)->ring == RingStyle::Battery) battery = model.device(i);
    }
    showRing(homeRing, battery != nullptr);
    float soc, pwr;
    if (battery && battery->online && battery->ringCount >= 2 && battery->valueAt(battery->ringPath[0], soc) &&
        battery->valueAt(battery->ringPath[1], pwr)) {
        updateRing(homeRing, battery);
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f %%", soc);
        lv_label_set_text(homeSoc, buf);
        snprintf(buf, sizeof(buf), "%+.0f W", pwr);  // + 충전, - 방전
        lv_label_set_text(homePwr, buf);
        lv_obj_set_style_text_color(homeSoc, lv_color_hex(C_SOC), 0);
    } else {
        if (battery) updateRing(homeRing, battery);
        lv_label_set_text(homeSoc, battery ? "battery offline" : mqttRef->connected() ? "waiting for battery..." : "");
        lv_obj_set_style_text_color(homeSoc, lv_color_hex(C_MUTED), 0);
        lv_label_set_text(homePwr, "");
    }

    char foot[24];
    snprintf(foot, sizeof(foot), "%u / %u online", model.onlineCount(), model.count());
    lv_label_set_text(homeFoot, model.count() ? foot : "");
    updateShortcuts();
}

// 페이지 점: 화면 아래쪽 가운데
void updateDots() {
    lv_obj_clean(dots);
    for (uint8_t p = 0; p <= tileCount; p++) {
        lv_obj_t* dot = lv_obj_create(dots);
        lv_obj_set_size(dot, p == page ? 14 : 8, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(p == page ? C_TEXT : C_MUTED), 0);
    }
}

void buildScreen() {
    lv_display_t* disp = lv_display_get_default();
    lv_display_set_theme(disp, lv_theme_default_init(disp, lv_color_hex(C_ACCENT), lv_color_hex(C_ON), true,
                                                     &lv_font_montserrat_14));
    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);

    tileview = lv_tileview_create(scr);
    lv_obj_set_style_bg_color(tileview, lv_color_hex(C_BG), 0);
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileview, onTileChanged, LV_EVENT_VALUE_CHANGED, nullptr);
    buildHome();
    buildShortcuts();

    dots = lv_obj_create(lv_layer_top());
    plain(dots);
    lv_obj_set_size(dots, 220, 12);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(dots, 6, 0);
    lv_obj_remove_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    updateDots();
}

}  // namespace

void DisplayService::begin(ConfigStore& store, MqttManager& mqtt, NetManager& net) {
    store_ = cfgStore = &store;
    mqtt_ = mqttRef = &mqtt;
    net_ = netRef = &net;
    model.begin(store.get().display.topicRoot);
    if (!panel.begin()) {
        LOG_E("display: panel '%s' failed to start", store.get().display.panel.c_str());
        return;
    }
    buildScreen();
#ifdef ESSIO_DISPLAY_DEMO
    // 화면 개발용 가짜 장치 (PLATFORMIO_BUILD_FLAGS=-DESSIO_DISPLAY_DEMO). 노드 없이 레이아웃을 확인한다.
    const char* demo[][2] = {
        {"rv/upower/meta", R"({"type":"upower","label":"Inverter","ring":"flows:pv.chg_w,grid.in_w,inv.out_w","switches":[{"n":"inverter","l":"Inverter"},{"n":"gridout_prio","l":"Grid Output Priority"},{"n":"solar_charge","l":"Solar Charge"},{"n":"grid_charge","l":"Grid Charge"}],"metrics":[{"l":"Battery SOC","u":"%","p":"bat.soc"},{"l":"PV Charge Power","u":"W","p":"pv.chg_w"},{"l":"Grid In Power","u":"W","p":"grid.in_w"},{"l":"Inverter Out Power","u":"W","p":"inv.out_w"},{"l":"Battery Voltage","u":"V","p":"bat.v"}]})"},
        {"rv/upower/state", R"({"pv":{"chg_w":1234},"inv":{"out_w":456},"grid":{"in_w":300},"bat":{"v":53.2,"soc":83}})"},
        {"rv/upower/availability", "online"},
        {"rv/upower/switch/inverter/state", "ON"},
        {"rv/upower/switch/solar_charge/state", "ON"},
        {"rv/bms/meta", R"({"type":"jbdbms","label":"BMS","ring":"battery:soc,power","switches":[{"n":"charge_fet","l":"Charge MOSFET"},{"n":"discharge_fet","l":"Discharge MOSFET"}],"metrics":[{"l":"SOC","u":"%","p":"soc"},{"l":"Power","u":"W","p":"power"},{"l":"Pack Voltage","u":"V","p":"pack_v"},{"l":"Current","u":"A","p":"current"},{"l":"Cell Voltage Diff","u":"V","p":"cell_diff"}]})"},
        {"rv/bms/state", R"({"soc":64,"power":-312.5,"pack_v":52.1,"current":-6.0,"cell_diff":0.012})"},
        {"rv/bms/availability", "online"},
        {"rv/bms/switch/charge_fet/state", "ON"},
        {"rv/rtu/meta", R"({"type":"rtusw_mk1","label":"Switch","switches":[{"n":"ch1","l":"Pump"},{"n":"ch2","l":"Lights"},{"n":"ch3","l":"Mover"},{"n":"ch4","l":"Drain"},{"n":"ch5","l":"Fill"},{"n":"ch6","l":"Heater"}],"metrics":[]})"},
        {"rv/rtu/availability", "offline"},
    };
    for (auto& m : demo) model.apply(m[0], m[1]);
#endif
    updateHome();
    lv_timer_handler();  // 첫 화면을 그린 뒤 백라이트를 켜 쓰레기 화면이 보이지 않게
    active_ = true;
    lastInputMs = millis();
    applyConfig();
}

void DisplayService::subscribeAll() {
    String f[8];
    uint8_t n = 0;
    model.filters(f, n);
    for (uint8_t i = 0; i < n; i++) mqtt_->subscribe(f[i]);
}

bool DisplayService::onMessage(const String& topic, const String& payload) {
    return model.apply(topic, payload);
}

void DisplayService::applyConfig() {
    if (!active_) return;
    dimmed = false;
    panel.setBrightness(store_->get().display.brightness);
    buildShortcuts();
    updateHome();
}

void DisplayService::tick() {
    if (!active_) return;
    const auto& dc = store_->get().display;
    const uint32_t now = millis();

    // 입력: 어두운 상태의 첫 입력은 깨우기만 한다
    int32_t rot = panel.takeRotation();
    bool click = panel.takeClick();
    bool touched = panel.lastTouchMs() != lastTouchSeen;
    lastTouchSeen = panel.lastTouchMs();
    if (rot || click || touched) {
        lastInputMs = now;
        if (dimmed) {
            dimmed = false;
            panel.setBrightness(dc.brightness);
            rot = 0;
            click = false;
        }
    }
    // 확인 창이 떠 있으면 노브는 취소로만 쓴다
    if (pending.overlay && (rot || click || now - pending.openedMs >= CONFIRM_TIMEOUT_MS)) {
        closeConfirm();
        rot = 0;
        click = false;
    }
    if (rot) {
        int32_t p = (int32_t)page + (rot > 0 ? 1 : -1);
        showPage((uint8_t)constrain(p, 0, (int32_t)tileCount));
    }
    if (click) showPage(0);
    if (!dimmed && !pending.overlay && dc.dimAfterS && now - lastInputMs >= dc.dimAfterS * 1000UL) {
        dimmed = true;
        panel.setBrightness(dc.dimBrightness);
        panel.swallowNextTouch();
    }

    // 모델 → 화면
    bool pagesChanged = false;
    while (tileCount < model.count()) {
        buildDeviceTile(tileCount++);
        pagesChanged = true;
    }
    bool switchesChanged = false, layoutChanged = pagesChanged;
    for (uint8_t i = 0; i < tileCount; i++) {
        DisplayDevice& d = *model.device(i);
        switchesChanged |= d.layoutDirty || d.valueDirty;
        layoutChanged |= d.layoutDirty;
        if (d.layoutDirty) buildDeviceTile(i);
        if (d.valueDirty) updateDeviceTile(i);
    }
    if (layoutChanged) arrangeTiles();  // meta가 와야 타입(=순위)을 안다
    if (switchesChanged) updateShortcuts();  // 바로가기 색은 1초 주기를 기다리지 않는다
    static uint8_t dotsPage = 0xFF;
    if (pagesChanged || dotsPage != page) {
        dotsPage = page;
        updateDots();
    }
    if (model.listChanged() || now - lastStatusMs >= 1000) {
        lastStatusMs = now;
        updateHome();
    }

    const uint32_t t0 = millis();
    uint32_t fUs, fPx, fCalls;
    CrowPanel21::takeFlushStats(fUs, fPx, fCalls);  // 이번 프레임만 재도록 비운다
    lv_timer_handler();
    // 그리기가 길어지면 MQTT 수신(루프 1회당 1건)이 밀린다 — 느린 프레임을 10초에 한 번 알린다
    static uint32_t lastSlowLogMs = 0;
    const uint32_t took = millis() - t0;
    if (took > 150 && now - lastSlowLogMs > 10000) {
        lastSlowLogMs = now;
        CrowPanel21::takeFlushStats(fUs, fPx, fCalls);
        LOG_W("display: frame took %lu ms (flush %lu ms, %lu px, %lu calls)", (unsigned long)took,
              (unsigned long)(fUs / 1000), (unsigned long)fPx, (unsigned long)fCalls);
    }
}

}  // namespace essio

#endif
