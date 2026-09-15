# 11. 신규 아키텍처

## 1. 계층 구조

```
┌──────────────────────────────────────────────────────────────┐
│  Web UI (data/index.html, gzip)   ←→  WebApi (REST JSON)     │
├──────────────────────────────────────────────────────────────┤
│  Core                                                        │
│   ConfigStore   NetManager(WiFi/AP/mDNS)   MqttManager       │
│   Scheduler     HaDiscovery                IoManager(btn/led)│
│   Logger        SysInfo                                      │
├──────────────────────────────────────────────────────────────┤
│  Device slot[1] → IDeviceModule (Upower | Jbdbms | RTU SW)   │
├──────────────────────────────────────────────────────────────┤
│  Ports[3] → SerialPort (HW UART1/2, SoftwareSerial) + Modbus │
└──────────────────────────────────────────────────────────────┘
```

## 2. 디렉터리 구조 (제안)

```
ess.io2/
  platformio.ini
  docs/
  data/                      # LittleFS 이미지: index.html.gz
  web/                       # UI 소스(빌드 → data/)
  src/
    main.cpp                 # setup/loop, 조립만
    core/
      Config.h/.cpp          # 스키마, 로드/저장/검증/마이그레이션
      NetManager.h/.cpp
      MqttManager.h/.cpp
      HaDiscovery.h/.cpp
      Scheduler.h/.cpp
      WebApi.h/.cpp
      IoManager.h/.cpp
      Logger.h/.cpp
      SysInfo.h/.cpp
    port/
      SerialPort.h/.cpp      # 포트 추상화 + Modbus 마스터 보유, 뮤텍스
    modules/
      IDeviceModule.h
      ModuleRegistry.h/.cpp  # type 문자열 → 팩토리
      upower/  Upower.h/.cpp  UpowerProtocol.h (순수 파서)
      jbdbms/  Jbdbms.h/.cpp  JbdFrame.h (순수 프레임/체크섬)
      rtusw/   RtuSwMk1.h/.cpp RtuSwMk2.h/.cpp
  test/
    test_jbd_frame/  test_upower_scale/
```

## 3. 핵심 인터페이스

### 3.1 `IDeviceModule`

```cpp
struct SwitchDef { const char* name; const char* label; };
struct SensorDef { const char* key; const char* label; const char* unit;
                   const char* devClass; const char* stateClass; const char* jsonPath; };

class IDeviceModule {
public:
  virtual ~IDeviceModule() {}
  virtual const char* type() const = 0;                // "upower"
  virtual bool begin(SerialPort& port, const JsonObjectConst& params) = 0;
  virtual void end() = 0;

  // 폴링: 호출마다 최대 1개 요청만 수행하고 진행 상태를 반환 (시간 예산 준수)
  enum PollResult { POLL_BUSY, POLL_DONE, POLL_ERROR };
  virtual PollResult pollStep() = 0;

  // 상태 → JSON (state 토픽 페이로드, 웹 상태 API 공용)
  virtual void toJson(JsonObject& out) const = 0;

  // 스위치
  virtual size_t switchCount() const = 0;
  virtual const SwitchDef& switchDef(size_t i) const = 0;
  virtual bool switchState(size_t i) const = 0;
  virtual bool writeSwitch(size_t i, bool on) = 0;     // 동기, 재시도 포함, 성공 시 캐시 갱신

  // Discovery 메타
  virtual size_t sensorCount() const = 0;
  virtual const SensorDef& sensorDef(size_t i) const = 0;

  // 헬스
  virtual uint32_t lastOkMs() const = 0;
  virtual uint16_t consecutiveErrors() const = 0;
};
```

### 3.2 `SerialPort`

```cpp
class SerialPort {
  // config: kind(hw1|hw2|sw), rx, tx, baud, de_pin(-1), timeout_ms
  bool begin(const PortConfig&);
  Stream& stream();
  ModbusMaster& modbus(uint8_t slaveId);   // slave 변경 시 begin() 재호출, 응답 버퍼 클리어
  bool lock(uint32_t waitMs);               // 슬롯 간 직렬화 (FreeRTOS mutex)
  void unlock();
};
```
- Modbus 응답 타임아웃은 `ModbusMaster` 상수라 수정이 필요하면 포크 또는 `ku16MBResponseTimeout`을 빌드 플래그로 재정의 (라이브러리 v2.0.1은 `#define`이 아닌 `static const` — 필요 시 경량 자체 구현 검토, #Q-11 연계).

### 3.3 `Slot`

```cpp
struct Slot {
  uint8_t index; bool enabled; String slug; String type;
  uint8_t portIndex; uint32_t pollIntervalMs;
  IDeviceModule* module;      // nullptr if disabled
  uint32_t nextPollMs; bool online; uint32_t lastPublishMs;
};
```

## 4. 실행 모델

- **단일 FreeRTOS 태스크(Arduino loop)** 를 유지하되 모든 컴포넌트는 `tick()` 비블로킹.
  (ESPAsyncWebServer는 자체 태스크; MQTT는 loop에서 `client.loop()`.)
- `loop()`:
  ```
  net.tick(); mqtt.tick(); io.tick(); scheduler.tick(); sysinfo.tick(); logger.tick();
  ```
- `Scheduler.tick()`:
  1. 명령 큐(스위치 쓰기)에 항목이 있으면 우선 처리 (포트 lock → writeSwitch → unlock → 상태 발행).
  2. `now >= slot.nextPollMs` 인 슬롯 중 포트가 idle인 슬롯 하나를 골라 `pollStep()` 1회 실행.
     - `POLL_BUSY`: 다음 tick에 계속.
     - `POLL_DONE`: `nextPollMs += interval`, 상태 발행(`<base>/<slot>/state`, 스위치 state), 온라인 전환.
     - `POLL_ERROR`: 오류 카운트++, 임계 초과 시 offline 발행, `nextPollMs = now + interval`.
  3. IO 출력 핀을 소스 슬롯 상태로 갱신.
- 모듈 내부 `pollStep()`은 단계 인덱스를 유지(예: Upower 0:coil×4 → 1..4: 블록 A~D). 단계당 Modbus 요청 1회 → 최악 블로킹 = 응답 타임아웃 1회.

## 5. 상태 머신

### 5.1 NetManager
```
BOOT → (ssid 있음) STA_CONNECTING ─성공→ STA_CONNECTED
                     │ 60s 실패
                     └→ AP_STA (AP 켜고 STA 재시도 계속)
BOOT → (ssid 없음) AP_ONLY
STA_CONNECTED ─끊김→ STA_CONNECTING
```
- 이벤트 기반(`WiFi.onEvent`), mDNS는 STA 연결 시 시작.
- 캡티브 포털: AP 모드에서 DNS 서버(모든 도메인 → AP IP).

### 5.2 MqttManager
```
DISABLED (host 없음)
IDLE → CONNECTING → CONNECTED ─끊김→ BACKOFF(1,2,4…30s) → CONNECTING
CONNECTED 진입 시: status=online, 구독(sys/cmd, 각 슬롯 set, homeassistant/status), Discovery 발행(옵션)
```

### 5.3 Slot lifecycle
```
DISABLED → (config enable) INIT → module.begin() 성공 → POLLING ↔ OFFLINE(연속오류) → (config change) END → INIT
```

## 6. 명령 경로

| 출처 | 진입 | 처리 |
|---|---|---|
| MQTT `<base>/<slot>/switch/<name>/set` | MqttManager 콜백 → `scheduler.enqueue({slot, switchIdx, on})` | 다음 tick에서 실행 |
| 웹 `POST /api/slots/{i}/switch/{name}` | WebApi → enqueue, 결과는 202 + 이후 상태 조회 | |
| 버튼 | IoManager 디바운스 → action → enqueue | |
| MQTT `<base>/sys/cmd` / 웹 `POST /api/system/restart` | 즉시(restart는 200 응답 후 500ms 뒤) | |

## 7. HaDiscovery
- 입력: 슬롯 목록 + 각 모듈의 `sensorDef/switchDef`.
- 출력: `07-mqtt-homeassistant.md` B.4 형식. 장치 종류·센서 노출 설정 변경 시 이전 config 토픽에 빈 retained 페이로드를 발행한다. 마지막 발행 토픽 목록은 LittleFS `/ha-discovery.json`에 기록한다.
- 레거시 정리 버튼: `03-device-upower.md` §6.5 + jbdbms/rtusw 레거시 토픽에 빈 페이로드 1회 발행.

## 8. ConfigStore
- `/config.json`(LittleFS) 로드 → 스키마 버전 확인 → 마이그레이션 → 검증 → 메모리 구조체.
- 저장은 원자적(임시 파일 → rename).
- 변경 적용 범위 판단: `wifi.*`→ 재접속, `mqtt.*`→ MQTT 재접속+Discovery, `ports[i]`→ 해당 포트 사용 슬롯 재초기화, `slots[i]`→ 슬롯 재초기화, `io.*`→ IoManager 재설정, `web.auth`→ 즉시.

## 9. 오류 처리·복원
- 재부팅은 워치독, 사용자 명령, factory_reset에서만.
- 힙 < 20KB 시 WARN 로그 + sys/info에 표기.
- 부팅 루프 방지: NVS 부트 카운터가 5분 내 5회 초과 시 안전 모드(모든 슬롯 비활성, 웹만).

## 10. 라이브러리 (확정)
- `ESP32Async/ESPAsyncWebServer ^3.7` + `ESP32Async/AsyncTCP ^3.3` (Q-2 확정)
- `bblanchon/ArduinoJson ^7`
- `knolleary/PubSubClient ^2.8` (버퍼 2048)
- Modbus RTU: **자체 구현** `src/port/ModbusRtu.*` (FC01/03/04/05/06/16 + CRC16, 슬레이브·타임아웃을 호출마다 지정 가능). `ModbusMaster`는 타임아웃이 상수라 사용하지 않음
- `plerup/EspSoftwareSerial ^8` (SW 포트만)
- 코어 내장: `LittleFS`, `Preferences`, `ESPmDNS`, `DNSServer`, `WiFi`
- 플랫폼: `espressif32 @ ^6.9` (Arduino core 2.0.x)

## 11. 스레드 경계
| 태스크 | 실행 내용 | 공유 자원 접근 규칙 |
|---|---|---|
| Arduino `loop` | Net/Mqtt/Io/Scheduler tick, 모듈 poll, MQTT 콜백, 설정 적용 | 소유자. 자유롭게 접근 |
| AsyncTCP (웹) | HTTP 핸들러 | `Scheduler::snapshot()`(뮤텍스), `Scheduler::enqueueSwitch()`(FreeRTOS 큐), `WebApi::pending*`(뮤텍스)만 사용. 파일 쓰기·시리얼 I/O·재부팅 금지 |
