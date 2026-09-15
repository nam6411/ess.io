# 01. 기존 시스템(ess.io) 전체 동작 스펙

> 소스: `ess.io/src/main.cpp`, `common.h`, `device.h/.cpp`. 기준 커밋 `d631413`.
> 이 문서는 "기존에 어떻게 동작했는가"를 빠짐없이 기록한다. 개선안은 `10-`, `11-` 문서에 있다.

---

## 1. 빌드 환경

- PlatformIO, `framework = arduino`
- 환경 2개: `esp32dev`(espressif32), `esp8266`(esp12e). 신규는 ESP32만.
- 라이브러리 (버전 고정값):
  - `bblanchon/ArduinoJson @ ^6.19.4`
  - `plerup/EspSoftwareSerial @ ^6.16.1`
  - `knolleary/PubSubClient @ ^2.8`
  - `4-20ma/ModbusMaster @ ^2.0.1`
  - `links2004/WebSockets @ ^2.3.7`
- 모니터 115200bps, `monitor_rts = 0`, `monitor_dtr = 0`
- 상수 (`common.h`)

| 상수 | 값 | 용도 |
|---|---|---|
| `CURRENT_VERSION` | `"0.1.1"` | HA device `sw_version` |
| `NUM_MAX_CELL` | 16 | BMS 최대 셀 수 |
| `NUM_SWITCH` | 3 | (미사용) |
| `NUM_RTU_MK1_SW` | 8 | Mk1 채널 배열 크기 |
| `NUM_RTU_MK2_SW` | 4 | Mk2 채널 배열 크기 |
| `MQTT_CLIENT_NAME` | `"ESP32"` | MQTT 클라이언트ID 접두어 |
| `MQTT_MAX_PACKET_SIZE` | 1024 | PubSubClient 버퍼 크기 |
| `INVERTER_STATE_PIN` | 25 | 인버터 상태 출력 |
| `MOVER_STATE_PIN` | 26 | 무버 상태 출력 |
| `INVERTER_BUTTON_PIN` | 12 | 인버터 버튼 입력 |
| `MOVER_BUTTON_PIN` | 14 | 무버 버튼 입력 |

---

## 2. 전역 객체 / 상태

| 이름 | 타입 | 설명 |
|---|---|---|
| `server` | `WebServer(80)` | HTTP 서버 |
| `webSocket` | `WebSocketsServer(81)` | WebSocket 서버 (스텁) |
| `BOARD_NAME` | `String` | `"esp32"` / `"esp8266"`. AP SSID, mDNS 호스트명 |
| `device_id` | `char[300]` | 6자리 HEX 칩 ID |
| `ssid, ssid_pw, mqtt_addr, mqtt_port, mqtt_client_id, mqtt_client_pw` | `char*` (각 malloc 30B) | EEPROM에서 읽은 설정 |
| `wifiClient`, `mqttClient` | `WiFiClient`, `PubSubClient` | MQTT 커넥션 |
| `devices[10]`, `numOfDevice` | `Device*[]` | 등록된 장치 목록 (실제 3개) |
| `devUpower`, `devRtuSwitch` | `Upower*`, `RtuSwMk1*` | 버튼/LED 연동을 위해 별도 보관 |
| `serialUpower`, `serialSwitch`, `serialBms` | `SoftwareSerial` | 3개 소프트웨어 UART |
| `modbusUpower`, `modbusSwitch` | `ModbusMaster` | 2개 Modbus 마스터 |
| `queueMover`, `queueInverter` | `int` (초기 -1) | 버튼 ISR 카운터 |
| `lastMsg` | `long` | 마지막 폴링 시각 |
| `loop_count` | `int` | 라운드로빈 인덱스 |
| `COUNT_RESET_TIME` | 1000 | `detectMultiplePress` 윈도우 (미사용 함수) |

---

## 3. 부팅 시퀀스 (`setup()`) — 실행 순서 그대로

1. `Serial.begin(115200)`, `EEPROM.begin(4096)`
2. 버튼 핀 `INPUT_PULLUP`, 상태 핀 `OUTPUT`
3. `attachInterrupt(MOVER_BUTTON_PIN, Ext_MOVER_ISR, FALLING)`, `attachInterrupt(INVERTER_BUTTON_PIN, Ext_INVERTER_ISR, FALLING)`
4. `mqttClient.setBufferSize(1024)`, `mqttClient.setCallback(messageRouter)`
5. 설정 문자열 6개 각 30바이트 `malloc`
6. `read_params()` — EEPROM → 설정 문자열
7. `setup_wifi()` — §6 참조
8. HTTP 라우트 등록 후 `server.begin()`:
   - `GET /` → `webRootHandler` (**아무 응답도 보내지 않음**)
   - `GET /settings` → 설정 폼 HTML
   - `POST /setssid` → 설정 저장
   - `GET /restart` → 재부팅
   - `GET /reset` → HA 엔티티 재등록
   - 그 외 → 404 text/plain `"404: Not found"`
9. `MDNS.begin(BOARD_NAME)` → `esp32.local`
10. `device_id` 생성 (§4)
11. `webSocket.onEvent(webSocketEvent)`, `webSocket.begin()`
12. SoftwareSerial 3개 시작 (ESP32):
    - `serialUpower.begin(115200, SWSERIAL_8N1, RX=22, TX=23, invert=false, buf=256)`
    - `serialBms.begin(9600, SWSERIAL_8N1, RX=16, TX=17, false, 256)`
    - `serialSwitch.begin(9600, SWSERIAL_8N1, RX=18, TX=19, false, 256)`
13. `delay(100)`
14. Modbus pre/postTransmission 콜백 등록:
    - pre: `serial.enableIntTx(true)`, post: `serial.enableIntTx(false)`
      (SoftwareSerial 송신 중 인터럽트 기반 TX 활성화/비활성화 — RS485 DE 핀 제어가 아님)
15. `modbusUpower.begin(slave=10, serialUpower)`, `modbusSwitch.begin(slave=255, serialSwitch)`
16. `delay(100)`
17. 장치 인스턴스 생성 및 등록 순서 (인덱스 = 폴링 순서):
    - `devices[0] = devUpower = new Upower(&mqttClient, &modbusUpower)`
    - `devices[1] = new Jbdbms(&mqttClient, &serialBms, slave_id=0, cell_series=16)`
    - `devices[2] = devRtuSwitch = new RtuSwMk1(&mqttClient, &modbusSwitch, slaveID=0, numOfSW=8, "Equalizer", "Plumbing Drain", "Tank Drain", "Whale to Fill", "Aroundview", "Mover", "12v Charger")`
      → 채널명은 **7개만 전달** (8번째는 미초기화)
18. `setup_mqtt()` — §7 참조. **`setup_entity()`(HA Discovery 등록)는 부팅 시 호출되지 않는다.** 등록은 `/reset` 또는 `homeassistant/switch/reset/set` 수신 시에만 수행.

---

## 4. device_id 생성 규칙

```c
// ESP32
uint32_t id = 0;
for (int i = 0; i < 17; i += 8)
    id |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
sprintf(device_id, "%06X", id);
```
- eFuse MAC 48비트 중 `mac[47:40]`→bit0-7, `mac[39:32]`→bit8-15, `mac[31:24]`→bit16-23 를 조합한 24비트 값을 6자리 대문자 HEX로.
- `Device` 생성자에서도 동일 계산을 반복하여 각 인스턴스가 `device_id[7]`를 따로 보유.
- 사용처: MQTT 클라이언트ID(`ESP32-<id>`), HA `device.identifiers`, 엔티티 `uniq_id` 접두/접미.

---

## 5. 메인 루프 (`loop()`)

매 반복:

```
server.handleClient();
mqttClient.loop();
webSocket.loop();
now = millis();

if (queueMover > 5)    { 무버 토글(§5.2); queueMover = queueInverter = 0; }
if (queueInverter > 5) { 인버터 토글(§5.2); queueInverter = queueMover = 0; }

if (now - lastMsg > 2000) {
    lastMsg = now;
    queueMover = queueInverter = 0;          // 2초마다 버튼 카운터 강제 리셋
    device_cycle(loop_count++);
    loop_count %= numOfDevice;
}
```

### 5.1 폴링 스케줄 (`device_cycle(seq)`)

- **2초마다 장치 1개**를 라운드로빈으로 처리. 장치 3개이면 각 장치의 갱신 주기 = 6초.
- 순서:
  1. `updateStates(seq)`:
     - `devices[seq]->update_switch()`
     - `devices[seq]->update_data()`
     - `digitalWrite(INVERTER_STATE_PIN, devUpower->switch_state[INVERTER])`
     - `digitalWrite(MOVER_STATE_PIN, devRtuSwitch->getSwitchState(6))` ← Mk1 채널 6 ("Mover")
  2. WiFi 연결 확인:
     - 연결됨 & MQTT 연결됨 → `pushMQTT(seq)`: `publish_switch()` → `publish_data()`
     - 연결됨 & MQTT 끊김 → `setup_mqtt()` (재접속 시도, 이번 사이클 발행 생략)
     - WiFi 끊김 → `setup_wifi()` → `setup_mqtt()` (블로킹, 최대 40초)
- 모든 `update_*`/`publish_*`는 **동기·블로킹**. Modbus 타임아웃(ModbusMaster 기본 2000ms) × 요청 횟수만큼 루프가 멈출 수 있음.
- `Device::mqtt_publish()`는 매 호출마다 `delay(100)` 후 발행. Upower는 1사이클에 발행 9회(스위치 4 + 데이터 5) → 최소 0.9초 블로킹.

### 5.2 물리 버튼 동작

- ISR (`IRAM_ATTR`): FALLING 엣지마다 카운터 `++` (그리고 `Serial.printf` — ISR 내 직렬 출력, 위험).
- `loop()`에서 카운터 `> 5` 이면 토글:
  - 무버: `cur = devRtuSwitch->getSwitchState(6)`; `devRtuSwitch->change_switch("06", !cur ? "ON" : "OFF")`
  - 인버터: `cur = devUpower->switch_state[INVERTER]`; `devUpower->change_switch("inverter", !cur ? "ON":"OFF")`
- 토글 후 두 카운터 모두 0. 2초 폴링 틱마다도 0으로 리셋.
- 결과적으로 **"2초 윈도우 내에 FALLING 엣지 6회 이상"** 이 트리거 조건. 디바운스가 없어 한 번 누를 때 채터링으로 여러 엣지가 발생하는 것을 이용한 임계값(커밋 `eb2cb59 set button's threshold`).
- 카운터 초기값 -1 (첫 엣지 후 0).
- `detectMultiplePress()` (1초 윈도우, 최근 5회 누름 시각 링버퍼, ON일 때 2회/OFF일 때 3회 이상 → 토글)는 정의만 있고 **호출되지 않음**.

### 5.3 상태 출력 핀

- `INVERTER_STATE_PIN(25)` = UPOWER 인버터 코일 상태 (HIGH=ON)
- `MOVER_STATE_PIN(26)` = Mk1 채널 6 상태 (HIGH=ON)
- 폴링 사이클마다 갱신 (어느 장치를 폴링하든 매번 두 핀 모두 갱신)

---

## 6. WiFi (`setup_wifi()`)

```
if (WiFi.status() == WL_CONNECTED) return 0;
if (strlen(ssid) > 0 && strlen(ssid_pw) > 0) {
    count = 0
    while (!connected) {
        WiFi.begin(ssid, ssid_pw);
        20 × delay(500)   // 10초 대기
        if (count > 3) break;   // 최대 5회 시도 (count 0..4) ≈ 50초
        count++;
    }
    if (connected) { 로그 } else { isSTA = true; }   // ← AP는 켜지지 않음
} else {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(BOARD_NAME, "12341234");           // SSID "esp32", PW "12341234"
    isSTA = true;
}
return WiFi.status();
```

- SSID/PW가 **비어 있을 때만** AP 모드(`esp32` / `12341234`, IP `WiFi.softAPIP()` 기본 192.168.4.1).
- SSID가 설정되어 있으나 접속 실패 시 → AP를 켜지 않으므로 **재설정 수단 없음** (버그, `08-legacy-issues.md` #L-05).
- `WiFi.mode()`를 STA 케이스에서 명시하지 않음 (기본 STA).
- `WiFi.setAutoReconnect`, hostname 설정 없음.

---

## 7. MQTT 연결 (`setup_mqtt()`)

```
if (WiFi 미연결) return;
if (mqtt_addr, mqtt_port, mqtt_client_id, mqtt_client_pw 중 하나라도 빈 문자열) return;  // 익명 브로커 불가
mqttClient.setServer(mqtt_addr, atoi(mqtt_port));
client_name = "ESP32-" + device_id;         // char[20]
if (mqttClient.connect(client_name, mqtt_client_id, mqtt_client_pw)) {
    subscribe("homeassistant/switch/reset/set");
    subscribe("homeassistant/switch/restart/set");
    for each device: device->subscribe();   // 각 장치 subscribe_list 구독 (Device::subscribe는 같은 토픽을 2번 구독)
} else {
    log("failed, rc=%d")                    // 재시도는 다음 device_cycle에서
}
```

- LWT(will), keepalive, clean session 설정 없음 (PubSubClient 기본: keepalive 15s).
- TLS 없음.

### 7.1 수신 라우팅 (`messageRouter(topic, payload, len)`)

1. payload를 `char message[20]`에 복사 (**길이 검사 없음** — 20바이트 초과 시 오버플로).
2. topic을 `strtok(topic, "/")`로 분해: `homeassistant` / `<device_type>` / `<device_domain>` / `<switch_name>`
3. 모든 장치에 대해 `strcmp(device_domain, device->getDeviceName()) == 0` 이면 `device->change_switch(switch_name, message)`.
4. `device_domain == "restart"` → `resetFunc()` (주소 0 함수 포인터 호출).
   `device_domain == "reset"` → `setup_mqtt_device()` = 모든 장치 `setup_entity()`.

장치별 `getDeviceName()` 값: Upower `"upower"`, Jbdbms `"jbdbms_<slave_id>"` (예: `jbdbms_0`), RtuSwMk1 `"rtusw_mk1"`, RtuSwMk2 `"rtusw_mk2"`.

---

## 8. 웹 서버 (포트 80)

### `GET /`
핸들러 본문 비어 있음 → **응답 없음**(클라이언트 타임아웃).

### `GET /settings`
`text/html` 폼. 현재 값이 `value=""`에 채워짐. 필드:

| name | 설명 |
|---|---|
| `ssid` | WiFi SSID |
| `password` | WiFi 비밀번호 (type=text, 평문 노출) |
| `mqtt_address` | 브로커 호스트/IP |
| `mqtt_port` | 브로커 포트 (문자열 저장, `atoi`) |
| `mqtt_id` | MQTT 사용자명 |
| `mqtt_password` | MQTT 비밀번호 |

- 폼 `action="/setssid" method="post"`
- 두 번째 폼: `action="/reset"` (GET) 버튼 "Reset"
- `<input type=button value=send onclick=send()>` — WebSocket으로 `"aaaa"` 전송 (데모)
- 인라인 JS: `ws://<host>:81/` 접속, open 시 alert 2회 + `"My name is John"` 전송, 수신 시 `<p>set ssid : <data></p>` 추가. 실제 기능 없음.

### `POST /setssid`
- 6개 인자 모두 존재해야 함. 아니면 `400 text/plain "400: Invalid Request"`.
- 각 값을 `char[30]`에 `strcpy` (**길이 검사 없음**).
- `setWifiInfo()` → EEPROM 저장 (§9) → `200 "<h1>Set Success as <ssid>!</h1>"` → `read_params()` → `setup_wifi()` → `setup_mqtt()`.
- WiFi가 이미 연결되어 있으면 `setup_wifi()`는 즉시 return → 새 SSID가 즉시 적용되지 않음 (재부팅 필요).

### `GET /restart`
`200 "<h1>will be restart!</h1>"` 후 `resetFunc()`.

### `GET /reset`
`200 "<h1>will be reset all entity!</h1>"` 후 모든 장치 `setup_entity()` (HA Discovery 재발행). `delete_params()` 호출은 주석 처리됨 — 설정은 지워지지 않음.

### 인증
없음. HTTPS 없음.

### WebSocket (포트 81)
`webSocketEvent`: CONNECTED/DISCONNECTED/TEXT 를 시리얼 로그만. 서버→클라이언트 송신 없음.

---

## 9. EEPROM 설정 저장

- `EEPROM.begin(4096)` (ESP32는 NVS 위 에뮬레이션)
- 슬롯 6개, 오프셋 32바이트 간격:

| 오프셋 | 항목 | 최대 길이 |
|---|---|---|
| 0 | ssid | 30 (NUL + 체크섬 포함 32) |
| 32 | ssid_pw | 30 |
| 64 | mqtt_addr | 30 |
| 96 | mqtt_port | 30 |
| 128 | mqtt_client_id | 30 |
| 160 | mqtt_client_pw | 30 |

- 레코드 포맷: `<문자열 바이트...> 0x00 <checksum>`; `checksum = (char) Σ 문자 바이트` (8비트 합).
- `write_word(addr, s)`: 문자 순서대로 write, `addr+len` 에 `\0`, `addr+len+1` 에 checksum, `EEPROM.commit()`. 각 write 전 `delay(10)`.
- `read_word(addr, out)`: `addr`부터 최대 **35바이트**까지 스캔(슬롯 경계 32 초과 가능), `\0` 만나면 다음 바이트를 체크섬으로 비교. 불일치 → `out[0]='\0'`, return -1. 미발견 → `out[0]='\0'`, return 0. 바이트마다 `delay(10)`.
- `delete_params()`: 6개 슬롯에 빈 문자열 기록 (호출처 없음).
- `getWifiInfo()`: 로그용, 호출처 없음.

---

## 10. `Device` 기반 클래스 공통 동작 (`device.h/.cpp`)

### 10.1 인터페이스 (순수가상)

| 메서드 | 역할 | 호출 시점 |
|---|---|---|
| `int update_switch()` | 장치에서 스위치 상태 읽기 | 폴링 1단계 |
| `int update_data()` | 장치에서 측정값 읽기 | 폴링 2단계 |
| `int publish_switch()` | 스위치 상태 MQTT 발행 | 폴링 3단계 |
| `int publish_data()` | 측정값 MQTT 발행 | 폴링 4단계 |
| `int change_switch(const char* name, const char* onoff)` | 스위치 쓰기 (`"ON"`/`"OFF"`) | MQTT 수신, 버튼 |
| `int setup_entity()` | HA Discovery config 발행 | `/reset`, `reset/set` |
| `const char* getDeviceName()` | 토픽 도메인 | 라우팅 |

### 10.2 공통 멤버

- `char* subscribe_list[20]`, `int subscribe_size` — 구독 토픽. `subscribe()`는 각 토픽을 2회 구독.
- `comm_info_t {rx_pin, tx_pin, baudrate, slaveID}` — 각 장치가 생성자에서 채움. 실제 시리얼 초기화는 `main.cpp`가 하므로 **`rx_pin/tx_pin/baudrate` 값은 무시됨** (`prepareSerial/prepareModbus` 호출부 전부 주석). `slaveID`만 토픽 이름에 사용.
- `char device_name[20]`, `char call_name[20]` — Jbdbms만 사용 (`"jbdbms"`, `"jbdbms_0"`).

### 10.3 `mqtt_publish(topic, message, retain=false)`

```
Serial.println(topic + "-> " + message + " retain : " + retain);
delay(100);
if (mqttClient->connected()) return mqttClient->publish(topic, message, retain);
else { ESP.restart(); return 0; }       // 미연결 시 즉시 재부팅
```

### 10.4 HA device 블록 (모든 엔티티 공통)

```json
{
  "identifiers": ["<device_id>"],
  "connections": [["mac", "<WiFi.macAddress()>"]],
  "name": "Battery",
  "model": "ESP32",          // char* 버전 및 mqtt_register()는 "ESP8266"
  "sw_version": "0.1.1",
  "manufacturer": "nam6411"
}
```

### 10.5 Discovery 페이로드 생성기 (레거시 방식)

`assemble_discover_switch_message(uniq_id, name, state_topic, command_topic)`:
```json
{"device": <device블록>, "uniq_id": "<device_id>_<uniq_id>", "name": "<name>",
 "state_topic": "<state_topic>", "command_topic": "<command_topic>"}
```

`assemble_discover_sensor_message(uniq_id, name, unit, device_class, state_topic, value_json)`:
```json
{"device_class": "<device_class>", "device": <device블록>, "uniq_id": "<device_id>_<uniq_id>",
 "name": "<name>", "state_topic": "<state_topic>", "unit_of_measurement": "<unit>",
 "value_template": "{{ value_json.<value_json>}}"}
```

`char*` 버전(버퍼 `char device_buf[1000]`)과 `String` 버전 두 벌 존재, 내용 동일.

### 10.6 `mqtt_register(name, device_class)` (신규 방식, Jbdbms만 사용)

- `entity = lower(replace(name, ' ', '_'))`
- `device_no = comm_info.slaveID` (문자열)
- 단위 자동 매핑:

| device_class | unit | 종류 |
|---|---|---|
| power | W | sensor |
| current | A | sensor |
| voltage | V | sensor |
| energy | Ah | sensor |
| temperature | ℃ | sensor |
| humidity | % | sensor |
| battery | % | sensor |
| ohm | Ω | sensor |
| switch | (없음) | switch |
| 그 외 | (없음) | sensor |

- sensor:
  - config topic: `homeassistant/sensor/<device_name>_<no>/<entity>/config`
  - payload: `device`, `uniq_id = "<entity>_<no>_<device_id>"`, `name`, `device_class`, `unit_of_measurement`, `value_template = "{{ value_json.<entity>}}"`, `state_topic = "homeassistant/sensor/<device_name>_<no>/<device_name>/state"`
- switch:
  - config topic: `homeassistant/switch/<device_name>_<no>/<entity>/config`
  - payload: `device`, `uniq_id`, `name`, `command_topic = "homeassistant/switch/<device_name>_<no>/<entity>/set"`, `state_topic = "homeassistant/switch/<device_name>_<no>/<entity>/state"`
- 발행 순서: 같은 config 토픽에 빈 페이로드(retain) → JSON(retain). (빈 페이로드 = HA에서 기존 엔티티 삭제)

### 10.7 토픽 헬퍼

| 함수 | 결과 |
|---|---|
| `getSensorTopicName(topic, end)` | `homeassistant/sensor/<device_name>_<slaveID>/<device_name>/<end>` (topic 인자 무시) |
| `getSwitchTopicName(topic, end)` | `homeassistant/switch/<device_name>_<slaveID>/<topic>/<end>` |
| `getFullTopic(topic, end)` | `homeassistant/switch/<device_name>_<device_name>_<slaveID>_<topic>/<end>` (미사용) |

---

## 11. 시리얼 로그 (참고)

거의 모든 단계에서 `Serial.printf`로 로그 출력. 신규 구현에서는 로그 레벨과 웹 UI 로그 뷰(선택)를 고려.
