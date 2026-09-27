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

struct DeviceTile {
    lv_obj_t* tile = nullptr;
    lv_obj_t* arc = nullptr;
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
lv_obj_t* homeList = nullptr;
lv_obj_t* homeFoot = nullptr;
lv_obj_t* dots = nullptr;
DeviceTile tiles[DeviceModel::MAX_DEVICES];
uint8_t tileCount = 0;
uint8_t page = 0;  // 0 = 홈, i+1 = 장치 i

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

// ---- 조작 ----

void showPage(uint8_t p, bool anim = true) {
    if (p > tileCount) p = tileCount;
    page = p;
    lv_tileview_set_tile_by_index(tileview, p, 0, anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

void onSwitch(lv_event_t* e) {
    const uintptr_t tag = (uintptr_t)lv_event_get_user_data(e);
    DisplayDevice* d = model.device(tag >> 8);
    if (!d || (tag & 0xFF) >= d->switchCount) return;
    const DisplaySwitch& s = d->switches[tag & 0xFF];
    const bool want = !s.on;
    // 상태는 노드가 다시 발행하는 switch/<name>/state로 확정된다(여기서 먼저 바꾸지 않음)
    mqttRef->publish(d->prefix + "/switch/" + s.name + "/set", want ? "ON" : "OFF");
    LOG_I("display: %s %s -> %s", d->label.c_str(), s.name.c_str(), want ? "ON" : "OFF");
}

void onTileChanged(lv_event_t*) {
    lv_obj_t* active = lv_tileview_get_tile_active(tileview);
    page = active == homeTile ? 0 : page;
    for (uint8_t i = 0; i < tileCount; i++) {
        if (tiles[i].tile == active) page = i + 1;
    }
}

// ---- 장치 화면 ----

void buildDeviceTile(uint8_t i) {
    DisplayDevice& d = *model.device(i);
    DeviceTile& t = tiles[i];
    if (!t.tile) t.tile = lv_tileview_add_tile(tileview, i + 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_clean(t.tile);
    t = DeviceTile{t.tile};

    // 대표값이 %면 테두리 링으로 보인다 (SOC 등)
    t.arc = lv_arc_create(t.tile);
    lv_obj_set_size(t.arc, 456, 456);
    lv_obj_center(t.arc);
    lv_arc_set_bg_angles(t.arc, 135, 45);
    lv_arc_set_range(t.arc, 0, 100);
    lv_obj_remove_style(t.arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(t.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(t.arc, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(t.arc, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(t.arc, lv_color_hex(0x1F1F23), LV_PART_MAIN);
    lv_obj_set_style_arc_color(t.arc, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    const bool ring = d.metricCount > 0 && d.metrics[0].unit == "%";
    if (!ring) lv_obj_add_flag(t.arc, LV_OBJ_FLAG_HIDDEN);

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
    const uint32_t valueColor = d.online ? C_TEXT : C_MUTED;
    if (t.hero) {
        lv_label_set_text(t.hero, metricText(d, 0, true).c_str());
        lv_obj_set_style_text_color(t.hero, lv_color_hex(valueColor), 0);
        float v;
        if (d.metricValue(0, v)) lv_arc_set_value(t.arc, (int32_t)constrain(v, 0.0f, 100.0f));
    }
    for (uint8_t m = 0; m < SECONDARY; m++) {
        if (!t.mVal[m]) continue;
        lv_label_set_text(t.mVal[m], metricText(d, m + 1, true).c_str());
        lv_obj_set_style_text_color(t.mVal[m], lv_color_hex(valueColor), 0);
    }
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
    label(homeTile, &lv_font_montserrat_28, C_TEXT, LV_ALIGN_TOP_MID, 0, 56);
    lv_label_set_text(lv_obj_get_child(homeTile, 0), "ESS");
    homeStatus = label(homeTile, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_text_align(homeStatus, LV_TEXT_ALIGN_CENTER, 0);
    homeList = label(homeTile, &lv_font_montserrat_20, C_TEXT, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_style_text_align(homeList, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(homeList, 10, 0);
    homeFoot = label(homeTile, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_BOTTOM_MID, 0, -70);
    lv_obj_set_style_text_align(homeFoot, LV_TEXT_ALIGN_CENTER, 0);
}

void updateHome() {
    const Config& cfg = cfgStore->get();
    String st;
    if (netRef->staConnected()) st = String(LV_SYMBOL_WIFI " ") + netRef->connectedSsid();
    else if (netRef->apActive()) st = String(LV_SYMBOL_WIFI " setup: ") + netRef->apSsid() + "\n" + netRef->apIp();
    else if (!cfg.wifi.ssid.length() && !cfg.wifi.fallbackSsid.length()) st = LV_SYMBOL_WIFI " not configured";
    else st = LV_SYMBOL_WIFI " connecting...";
    st += mqttRef->connected() ? "   MQTT " LV_SYMBOL_OK : "   MQTT " LV_SYMBOL_CLOSE;
    lv_label_set_text(homeStatus, st.c_str());
    lv_obj_set_style_text_color(homeStatus, lv_color_hex(mqttRef->connected() ? C_MUTED : C_WARN), 0);

    // 장치마다 대표값 한 줄 (최대 5줄)
    String list;
    uint8_t shown = 0;
    for (uint8_t i = 0; i < model.count() && shown < 5; i++) {
        const DisplayDevice& d = *model.device(i);
        if (shown++) list += "\n";
        list += d.label + "  ";
        list += !d.online ? String("offline") : d.metricCount ? metricText(d, 0, true) : String(d.switchCount) + " sw";
    }
    if (!model.count()) list = mqttRef->connected() ? "waiting for devices..." : "";
    lv_label_set_text(homeList, list.c_str());

    char foot[48];
    snprintf(foot, sizeof(foot), "%u / %u online\nturn knob to browse", model.onlineCount(), model.count());
    lv_label_set_text(homeFoot, model.count() ? foot : "");
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
        {"rv/upower/meta", R"({"type":"upower","label":"UPower","switches":[{"n":"inverter","l":"Inverter"},{"n":"gridout_prio","l":"Grid Output Priority"},{"n":"solar_charge","l":"Solar Charge"},{"n":"grid_charge","l":"Grid Charge"}],"metrics":[{"l":"Battery SOC","u":"%","p":"bat.soc"},{"l":"PV In Power","u":"W","p":"pv.in_w"},{"l":"Inverter Out Power","u":"W","p":"inv.out_w"},{"l":"Grid In Power","u":"W","p":"grid.in_w"},{"l":"Battery Voltage","u":"V","p":"bat.v"}]})"},
        {"rv/upower/state", R"({"pv":{"in_w":1234},"inv":{"out_w":456},"grid":{"in_w":0},"bat":{"v":53.2,"soc":83}})"},
        {"rv/upower/availability", "online"},
        {"rv/upower/switch/inverter/state", "ON"},
        {"rv/upower/switch/solar_charge/state", "ON"},
        {"rv/bms/meta", R"({"type":"jbdbms","label":"JBD BMS","switches":[{"n":"charge_fet","l":"Charge MOSFET"},{"n":"discharge_fet","l":"Discharge MOSFET"}],"metrics":[{"l":"SOC","u":"%","p":"soc"},{"l":"Power","u":"W","p":"power"},{"l":"Pack Voltage","u":"V","p":"pack_v"},{"l":"Current","u":"A","p":"current"},{"l":"Cell Voltage Diff","u":"V","p":"cell_diff"}]})"},
        {"rv/bms/state", R"({"soc":64,"power":-312.5,"pack_v":52.1,"current":-6.0,"cell_diff":0.012})"},
        {"rv/bms/availability", "online"},
        {"rv/bms/switch/charge_fet/state", "ON"},
        {"rv/rtu/meta", R"({"type":"rtusw_mk1","label":"RTU Relay","switches":[{"n":"ch1","l":"Pump"},{"n":"ch2","l":"Lights"},{"n":"ch3","l":"Fridge"},{"n":"ch4","l":"Heater"},{"n":"ch5","l":"Fan"},{"n":"ch6","l":"Mover"}],"metrics":[]})"},
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
    if (rot) {
        int32_t p = (int32_t)page + (rot > 0 ? 1 : -1);
        showPage((uint8_t)constrain(p, 0, (int32_t)tileCount));
    }
    if (click) showPage(0);
    if (!dimmed && dc.dimAfterS && now - lastInputMs >= dc.dimAfterS * 1000UL) {
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
    for (uint8_t i = 0; i < tileCount; i++) {
        DisplayDevice& d = *model.device(i);
        if (d.layoutDirty) buildDeviceTile(i);
        if (d.valueDirty) updateDeviceTile(i);
    }
    static uint8_t dotsPage = 0xFF;
    if (pagesChanged || dotsPage != page) {
        dotsPage = page;
        updateDots();
    }
    if (model.listChanged() || now - lastStatusMs >= 1000) {
        lastStatusMs = now;
        updateHome();
    }

    lv_timer_handler();
}

}  // namespace essio

#endif
