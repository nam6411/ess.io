# 14. 사용 설명서

ess.io2 펌웨어를 빌드·설치·설정·운영하는 절차. 개발 참여자용 안내는 §9.

---

## 1. 준비물

| 항목 | 내용 |
|---|---|
| 보드 | ESP32 (esp32dev 호환, 4MB 플래시). ESP32-S2/S3/C3는 핀맵·파티션 검토 필요 |
| PC | Linux/macOS/Windows, Python 3.9+ |
| 도구 | PlatformIO Core (§2). VS Code + PlatformIO IDE 확장을 써도 됨 |
| 네트워크 | 2.4GHz WiFi AP, MQTT 브로커(예: Home Assistant Mosquitto 애드온) |
| 주변장치 | UPower 인버터(RS485), JBD BMS(UART TTL), Modbus RTU 릴레이 보드 — 필요한 것만 |
| RS485 모듈 | 자동 방향 전환형 권장. 수동(DE/RE) 모듈은 포트 설정 `de_pin` 지정 |

## 2. 도구 설치

이 저장소를 만든 환경에는 PlatformIO가 가상환경 `~/.platformio-venv`에 설치되어 있다.

```bash
# 최초 1회 (다른 PC에서)
python3 -m venv ~/.platformio-venv
~/.platformio-venv/bin/pip install platformio

# 편의상 PATH 추가 (bash)
echo 'export PATH="$HOME/.platformio-venv/bin:$PATH"' >> ~/.bashrc && source ~/.bashrc
pio --version
```

Linux에서 USB 시리얼 권한:
```bash
sudo usermod -aG dialout $USER   # 재로그인 필요
```

## 3. 빌드

### 3.1 빌드 환경

| env | 타겟 | 파티션 | 비고 |
|---|---|---|---|
| `esp32s3` (기본) | ESP32-S3 N16R8 (`esp32-s3-devkitc-1`) | `partitions_16mb.csv` — OTA 4MB ×2 + LittleFS 7.9MB | 설계서 §2·§8의 실제 타겟. 옥탈 PSRAM(`qio_opi`) |
| `esp32dev` | 구형 ESP32 | `partitions.csv` — OTA 1.5MB ×2 + LittleFS 960KB | 초기 개발용 |
| `native` | 호스트 PC | — | 프레임·CRC 단위 테스트 |

```bash
cd ess.io2
pio run                     # 기본 env(esp32s3) 펌웨어
pio run -e esp32dev         # 구형 ESP32
pio run -t buildfs          # LittleFS 이미지 (data/ → littlefs.bin)
pio test -e native          # 순수 로직 단위 테스트
```

- 최초 빌드는 툴체인 다운로드로 5분 이상 걸린다. 이후 증분 빌드는 수십 초.
- 펌웨어 버전 문자열은 `platformio.ini`의 `-DFW_VERSION`.

## 4. 업로드

```bash
# 시리얼 포트 확인
pio device list

pio run -t upload
pio run -t uploadfs

# 시리얼 모니터 (115200)
pio device monitor
```

포트 이름은 OS마다 다르다. 자동 감지가 실패하면 `--upload-port`로 직접 준다.

| OS | 포트 예 |
|---|---|
| macOS (네이티브 USB / USB-Serial-JTAG) | `/dev/cu.usbmodem*` |
| macOS (CP210x·CH340 브리지) | `/dev/cu.usbserial-*`, `/dev/cu.wchusbserial*` |
| Linux | `/dev/ttyACM0` (네이티브 USB) / `/dev/ttyUSB0` (브리지) |

```bash
pio run -t upload --upload-port /dev/cu.usbmodem101
```

- **S3의 두 USB 포트를 구분할 것.** devkit에는 네이티브 `USB`와 브리지 `COM`/`UART` 포트가 따로 있다.
  현재 `esp32s3` env는 `-DARDUINO_USB_CDC_ON_BOOT=1`이라 **로그가 네이티브 USB로 나간다.**
  브리지 포트로 로그를 보려면 그 값을 `0`으로 바꿔 다시 빌드한다.
- 열거가 안 되면: 데이터 케이블인지 확인 → 다른 USB 포트 → BOOT 버튼을 누른 채 꽂아 다운로드 모드로 진입.
- `uploadfs`는 `data/index.html`(웹 UI)을 올린다. **설정 파일 `/config.json`도 같은 파티션에 있으므로 `uploadfs`를 하면 설정이 지워진다.** 운영 중인 장치는 §8.3 설정 백업 후 진행.
- **보드를 `esp32dev` → `esp32s3`로 바꿨다면 첫 업로드 때 `uploadfs`를 같이 한다.** 예전 설정에 S3에 없는 핀(GPIO 22~25 등)이 들어 있으면 핀 검증에서 걸린다.
- 부팅 로그 예:
  ```
  [     123] info ess.io2 0.2.0-dev starting, device A1B2C3
  [     130] info config: /config.json not found
  [     131] warn config: using defaults
  [     132] info role: node
  [     140] info net: no SSID configured, AP only
  [     141] info net: AP RV-SETUP-A1B2C3 @ 192.168.4.1
  [     150] info port 0 (RS485): hw1 rx=18 tx=17 115200 bps
  [     155] info slot 0: upower 'upower' port=0 slave=10 every 5000 ms
  ...
  [     200] info web: started on :80
  ```

## 5. 최초 설정 (AP 모드)

1. 전원 인가. 설정이 없으면 AP `essio-<device_id>` (비밀번호 `12341234`)가 뜬다.
2. 스마트폰/PC로 AP에 접속 → 브라우저에서 `http://192.168.4.1/` (캡티브 포털 감지 시 자동으로 열림).
3. 현재 UI는 상태 확인·스위치 토글만 지원하므로 **WiFi/MQTT 설정은 API로 넣는다**:

```bash
# 현재 설정 받아서 파일로
curl -s http://192.168.4.1/api/config -o cfg.json

# cfg.json에서 wifi.ssid / wifi.password / mqtt.host / mqtt.username / mqtt.password 수정 후
curl -s -X PUT http://192.168.4.1/api/config \
     -H 'Content-Type: application/json' -H 'X-Requested-With: essio' \
     --data-binary @cfg.json
# → {"ok":true,"queued":true}
```

4. 저장 후 즉시 STA 접속을 시도한다. 연결되면 AP는 꺼지고(기본 `keep_when_sta_ok=false`) 이후 `http://essio-<device_id>.local/` 또는 공유기가 배정한 IP로 접속.
5. 접속 실패가 60초 이상 지속되면 AP가 다시 켜진다(STA 재시도는 계속). SSID/비밀번호를 다시 확인.

> `device_id`는 시리얼 부팅 로그 또는 AP 이름에서 확인. 비밀번호 필드를 `"********"`로 두면 기존 값이 유지된다.

### 5.1 최소 설정 예시

브로커 보드 — 라우터에 붙이고 내장 브로커를 켠다:
```json
{
  "device": { "role": "broker" },
  "wifi": { "ssid": "RV-LOCAL", "password": "secret" }
}
```

노드 보드 — 라우터(1순위)와 브로커 SoftAP(2순위)를 주면 브로커는 mDNS로 찾는다:
```json
{
  "wifi": { "ssid": "RV-LOCAL", "password": "secret",
            "fallback_ssid": "RV-FALLBACK", "fallback_password": "12341234" }
}
```

`PUT /api/config`는 보낸 키만 덮어쓰므로(부분 갱신) 나머지는 기본값(§7)이 유지된다.
역할과 드라이버는 `POST /api/system/mode` 한 번으로 바꾸는 편이 안전하다(§6).

## 6. 동작 모드 선택

이 펌웨어는 **브로커 호스트**로도, **장치 클라이언트**로도 동작한다. 보드마다 같은 펌웨어를
올리고 웹 UI에서 역할만 다르게 고른다. 자세한 동작 차이는 [15-roles.md](15-roles.md).

웹 대시보드 최상단의 "동작 모드" 드롭다운에서 고르고 적용을 누르면 된다. API로는:

```bash
curl -s http://$B/api/system/modes          # 선택지 + 현재 모드
curl -X POST $H $B/api/system/mode -d '{"mode":"broker"}'
```

| 모드 | 하는 일 |
|---|---|
| `broker` | 내장 MQTT 브로커(기본 1883). 장치 폴링 없음. mDNS `broker.local`로 자기를 알린다 |
| `upower` | UP5000 폴링 — 포트 0, 115200, Slave 10 |
| `jbdbms` | JBD BMS 폴링 — 포트 0, 9600 |
| `rtusw_mk1` / `rtusw_mk2` | RTU 릴레이 보드 — 포트 0, 9600 |
| `idle` | 아무것도 폴링하지 않음. 설정만 |

**역할(브로커↔노드)이 바뀌면 자동 재부팅**되고, 드라이버만 바뀌면 재부팅 없이 해당 슬롯만
다시 올라온다. 브로커 1대를 먼저 올려 두고 나머지 보드를 노드로 설정하는 순서가 편하다.

## 7. 기본 구성

설정 파일이 없을 때 적용되는 기본값 (`docs/12-config-schema.md` §1 전체 예시):

| 항목 | 기본값 |
|---|---|
| 역할 | `node` (장치 클라이언트) |
| 포트 0 | UART1, RX18/TX17, 115200 |
| 포트 1·2 | 비활성 |
| 슬롯 0 | `upower`, 슬레이브 10, 5초 |
| 슬롯 1~3 | 비활성 (보드 1대 = 장치 1대) |
| 버튼·출력 | 없음 (조작은 디스플레이 모듈 담당) |
| MQTT base | 노드 `rv/<슬롯 slug>` (예: `rv/upower`) / 브로커 `rv/broker` |
| 브로커 주소 | mDNS `broker.local` → NVS 마지막 성공 IP → 수동 `mqtt.host` |
| 2순위 Wi-Fi | `RV-FALLBACK` (브로커 SoftAP) |
| 웹 인증 | 비활성 |

여러 장치를 한 보드에 멀티드롭으로 물릴 수도 있다. 그때는 슬롯 1~3을 켜면 되고, 토픽에
`<base>/<slug>/...`처럼 slug 구간이 자동으로 붙는다.

## 8. 운영

### 8.1 웹 대시보드
`http://<ip>/` — 동작 모드 선택, 장치 정보(IP/RSSI/역할/토픽 접두/힙), 슬롯별 online 배지,
스위치 토글 버튼, 원시 상태 JSON. 브로커 모드에서는 접속 클라이언트 수·retained 보관 수·
메시지 카운터를 보여준다. 3초마다 갱신.

### 8.2 REST API 요약 (전체: `docs/13-web-api.md`)

```bash
H='-H Content-Type:application/json -H X-Requested-With:essio'
B=http://essio-A1B2C3.local

curl $B/api/system/info                       # 상태
curl "$B/api/system/log?since=0"              # 최근 로그 (링버퍼 100줄)
curl $B/api/slots                             # 슬롯 상태·스위치
curl $B/api/config                            # 설정 (비밀번호 마스킹)
curl $B/api/config/schema                     # 모듈 타입 목록·params 기본값
curl $B/api/system/modes                      # 동작 모드 목록 + 현재 모드

curl -X POST $H $B/api/system/mode -d '{"mode":"jbdbms"}'         # 모드 전환
curl -X POST $H $B/api/slots/0/switch/inverter -d '{"on":true}'   # 스위치
curl -X POST $H $B/api/slots/1/poll -d '{}'                       # 즉시 폴링
curl -X POST $H $B/api/system/rediscover                          # HA Discovery 재발행
curl -X POST $H $B/api/system/restart
curl -X POST $H $B/api/system/factory_reset -d '{"confirm":"RESET"}'
```

### 8.3 설정 백업/복원
```bash
curl -s $B/api/config -o backup.json            # 비밀번호는 마스킹됨 — 복원 시 다시 입력
curl -s -X PUT $H $B/api/config --data-binary @backup.json
```
(`export?secrets=1` 엔드포인트는 미구현.)

### 8.4 설정 변경 시 동작
| 바꾼 항목 | 결과 |
|---|---|
| `device.role` | **자동 재부팅** (브로커↔노드 전환) |
| `broker.*` | 브로커 재기동 (브로커 모드) |
| `wifi.*`, `device.hostname` | WiFi 재접속 (잠시 끊김) |
| `mqtt.*` | MQTT 재접속 + Discovery 재발행 |
| `ports[]` | 모든 포트·슬롯 재초기화 |
| `slots[]` | 슬롯 재초기화, 구독 갱신, Discovery 재발행 |
| `io.*` | 버튼/출력 핀 재설정 |
| `web.auth` | 즉시 적용 |

### 8.5 Home Assistant 연동
- 브로커에 연결되면 `homeassistant/<sensor|switch>/<device_id>_<slug>/<entity>/config` 에 Discovery를 발행하므로 HA MQTT 통합에 장치 `"ESS Gateway"`(설정 `device.name`)가 자동 생성된다.
- HA를 재시작하면 `homeassistant/status = online`을 받아 자동 재발행한다.
- 엔티티가 안 보이면: HA MQTT 통합에서 Discovery 활성 여부 확인 → `POST /api/system/rediscover` → `mosquitto_sub -t 'homeassistant/#' -v`로 config 토픽 확인.
- 상태 토픽(활성 슬롯 1개 기준): `rv/<slug>/state` (JSON), 스위치 `rv/<slug>/switch/<name>/state|set`,
  가용성 `rv/<slug>/status` · `rv/<slug>/availability`. 슬롯이 여러 개면 `<base>/<slug>/...`로 한 단계 깊어진다
  (→ [15-roles.md](15-roles.md) §4).
- **내장 브로커를 쓸 때는 HA를 그 브로커에 연결**하거나, 상위 브로커로 브리지해야 한다.
  AWS 브리지는 아직 미구현이므로, 지금은 HA가 브로커 노드에 직접 접속하는 구성만 동작한다.
- MQTT로 직접 제어:
  ```bash
  mosquitto_pub -h 192.168.0.10 -t essio/A1B2C3/relay/switch/ch6/set -m ON
  mosquitto_pub -h 192.168.0.10 -t essio/A1B2C3/sys/cmd -m rediscover   # restart | factory_reset
  ```

### 8.6 물리 버튼
- 기본: 짧게 누르면(디바운스 50ms) 매핑된 스위치 토글. `long_press_ms`를 설정하면 길게 누름에 별도 동작.
- 출력 핀은 매핑된 스위치 상태를 200ms 주기로 반영.

### 8.7 웹 인증
```json
{ "web": { "auth": { "enabled": true, "username": "admin", "password": "mypass" } } }
```
HTTP Basic Auth. HTTPS는 아니므로 신뢰 네트워크 내에서만 사용.

## 9. 문제 해결

| 증상 | 확인 |
|---|---|
| AP가 안 보임 | 시리얼 로그에 `net: connecting to ...` 가 있으면 SSID가 설정된 상태. 60초 후 AP가 켜짐. 즉시 초기화하려면 `factory_reset` 또는 `pio run -t uploadfs` |
| `.local` 접속 안 됨 | Android는 mDNS 미지원인 경우가 많음 → 공유기에서 IP 확인. `hostname` 설정으로 이름 변경 가능 |
| 슬롯이 계속 offline | `GET /api/slots` 의 `last_error`: `timeout`=응답 없음(배선/보레이트/슬레이브 주소), `crc`=노이즈/보레이트, `exception`=주소 범위 오류. 로그 레벨 `debug`로 올려 프레임 확인 |
| 노드가 브로커를 못 찾음 | `GET /api/system/info`의 `mqtt_address_source`: `none`이면 mDNS·NVS·수동 모두 실패. 브로커 보드가 켜져 있는지, 같은 망인지 확인 후 `mqtt.host`에 IP를 직접 넣는다 |
| 브로커 모드인데 클라이언트가 0 | 노드의 `mqtt.port`와 브로커의 `broker.port`가 같은지, `broker.username`을 설정했다면 노드에도 같은 자격이 들어갔는지 확인 |
| 재접속 후 HA 값이 비어 있음 | 브로커의 retained 보관 한도(`broker.retain_slots`, 기본 48)를 넘었을 수 있다. `/api/system/info`의 `broker.dropped` 확인 |
| RS485 수동 모듈에서 송신만 되고 수신 안 됨 | `ports[i].de_pin` 지정 |
| SoftwareSerial 포트에서 115200 불안정 | 115200은 하드웨어 UART(`hw1`/`hw2`)에 배정 |
| 부팅 직후 리셋 반복 | GPIO12 버튼(스트래핑 핀)이 눌린 상태로 부팅되면 실패 가능 → 버튼 핀 변경 |
| MQTT `backoff` 상태 | `mqtt_rc` 값: -2 네트워크, 4 인증 실패, 5 권한 없음. 브로커 로그 확인 |
| Discovery 엔티티 이름이 옛 ess.io와 중복 | 옛 토픽은 별도 uniq_id라 두 벌 생김 → HA에서 옛 장치 삭제 (`legacy_cleanup` 자동화는 미구현) |
| 설정이 저장 안 됨 | 로그에 `config: invalid: ...` 검증 오류 메시지 확인. `PUT`은 `202` 후 비동기 적용이므로 `/api/system/log`로 결과 확인 |

로그 레벨 변경: `PUT /api/config` `{"device":{"log_level":"debug"}}`.

## 10. 개발자 안내

### 10.1 새 장치 모듈 추가
1. `src/modules/<name>/` 에 `IDeviceModule` 구현 (`begin`, `pollStep`, `toJson`, 스위치/센서 정의).
2. `ModuleRegistry.cpp`의 `TYPES`, `create()`, `defaultParams()`에 등록.
3. `docs/`에 프로토콜 문서 추가, `12-config-schema.md`에 params 정의.
4. `pollStep()`은 **호출당 요청 1회**만 수행하고 `Busy`를 반환해 다음 tick에 이어간다 (메인 루프 블로킹 방지).

### 10.2 테스트
- 순수 로직(`Crc16.h`, `JbdFrame.h`)은 `test/test_frames`에 Unity 테스트. `pio test -e native`.
- 프로토콜 파서를 추가할 때는 Arduino 의존이 없는 헤더로 분리해 native에서 테스트 가능하게 한다.

### 10.3 스레드 규칙 (`docs/11-architecture.md` §11)
웹 핸들러(AsyncTCP 태스크)에서는 `Scheduler::snapshot/enqueueSwitch/switchState`, `ConfigStore::validate/toJson`만 호출. 파일 쓰기·시리얼·재부팅은 `WebApi::tick()`(메인 루프)로 넘긴다.

### 10.4 파티션
`partitions.csv`: app0/app1 각 1.5MB(OTA 대비), LittleFS 960KB. 플래시 4MB 미만 보드는 수정 필요.
