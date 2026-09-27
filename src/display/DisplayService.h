#pragma once
#include <Arduino.h>

#include "../core/Config.h"
#include "../core/MqttManager.h"
#include "../core/NetManager.h"

// Display 역할: 브로커의 노드 상태를 원형 터치 화면에 보이고 스위치를 조작한다. docs/15-roles.md §3.3
// 패널 드라이버와 LVGL은 ESSIO_DISPLAY 빌드(esp32s3)에만 들어간다. 그 밖의 빌드에서는
// 역할을 골라도 로그만 남기고 아무것도 하지 않는다.
namespace essio {

class DisplayService {
public:
    // 패널 초기화. 실패해도(패널 없음 등) MQTT·웹은 계속 동작한다
    void begin(ConfigStore& store, MqttManager& mqtt, NetManager& net);
    void subscribeAll();                                          // MQTT 재구독 목록에 필터 추가
    bool onMessage(const String& topic, const String& payload);  // 처리했으면 true
    void applyConfig();                                           // display.* 변경 (밝기·딤)
    void tick();
    bool active() const { return active_; }

private:
    ConfigStore* store_ = nullptr;
    MqttManager* mqtt_ = nullptr;
    NetManager* net_ = nullptr;
    bool active_ = false;
};

}  // namespace essio
