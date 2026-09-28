# 15. 동작 모드 (역할 선택)

하나의 펌웨어가 **브로커 호스트**, **장치 클라이언트**, **디스플레이** 중 하나로 동작한다.
어느 쪽인지는 컴파일 타임이 아니라 웹 UI에서 고른다 — 설계서 §2의 "예비 보드 한 장이 어느
노드든 대체한다"를 브로커 노드까지 확장한 것이다.

관련 소스: `src/main.cpp`(역할 분기), `src/core/BrokerService.*`, `src/core/NetManager.*`,
`src/core/MqttManager.*`, `src/core/WebApi.cpp`(모드 API).

---

## 1. 모드 목록

`GET /api/system/modes`가 반환하는 선택지. 웹 UI의 "동작 모드" 드롭다운이 이 목록을 그린다.

| id | 표시 | role | 결과 |
|---|---|---|---|
| `broker` | MQTT 호스트 (브로커) | broker | 내장 MQTT 브로커 기동. 장치 폴링·슬롯 없음 |
| `upower` | UP5000 (Modbus RTU) | node | 슬롯 0 = UP5000, 포트 0 = 115200, Slave 10 |
| `jbdbms` | JBD BMS | node | 슬롯 0 = JBD, 포트 0 = 9600 |
| `rtusw_mk1` | RTU 스위치 Mk1 (Coil) | node | 슬롯 0 = Mk1, 포트 0 = 9600, Slave 255 |
| `rtusw_mk2` | RTU 스위치 Mk2 (Register) | node | 슬롯 0 = Mk2, 포트 0 = 9600, Slave 1 |
| `mach` | MACH BMS (미구현·스니핑) | node | 슬롯 0 = MACH 더미, 포트 0 = 9600. 아무것도 보내지 않고 수신 바이트만 기록 (§3.4) |
| `display` | 디스플레이 (터치 화면) | display | 패널 기동, 모든 노드 구독. 슬롯·포트 없음 (§3.3). `supported:false`면 이 빌드에 패널 드라이버가 없음 |
| `idle` | 유휴 (발행 안 함) | node | 슬롯 전부 비활성. 설정만 가능한 상태 |

기본값은 드라이버별로 `ModuleRegistry`의 `ModuleTypeInfo`에 들어 있다(`slug`, `slave_id`, `baud`,
`poll_interval_ms`). 새 드라이버를 등록하면 모드 목록에 자동으로 나타난다.

## 2. 전환 방법

```bash
curl -X POST http://<ip>/api/system/mode \
     -H 'Content-Type: application/json' -H 'X-Requested-With: essio' \
     -d '{"mode":"broker"}'
# → 202 {"ok":true,"mode":"broker","restart_required":true}
```

한 번의 호출로 `device.role` + `slots[]` + `ports[0].baud` + `mqtt.base_topic`을 일관되게 맞춘다.
개별 필드를 `PUT /api/config`로 직접 고쳐도 되지만, 그때는 역할과 슬롯이 어긋나지 않게 직접 챙겨야 한다.

- **역할이 바뀌면 재부팅**한다(`restart_required: true`). 브로커·스케줄러·UART 소유권이 통째로
  바뀌므로 무중단 전환을 시도하지 않는다.
- **드라이버만 바뀌면 재부팅하지 않는다.** 해당 슬롯만 재초기화되고, MQTT 재접속 + 구독 갱신 +
  Discovery 재발행이 이어진다.
- `mqtt.base_topic`은 빈 문자열로 재설정되어 §4의 기본값 규칙이 다시 적용된다.

## 3. 역할별 동작

### 3.1 Broker (`device.role = "broker"`)

| 항목 | 내용 |
|---|---|
| MQTT | `PicoMQTT::Server`가 `broker.port`(기본 1883)에서 수신. 인증은 `broker.username`이 설정된 경우에만 |
| 클라이언트 수 | `broker.max_clients`(기본 8) 초과 시 `CRC_SERVER_UNAVAILABLE`로 거부 |
| 장치 폴링 | 없음. 슬롯·포트를 만들지 않아 UART 핀을 점유하지 않는다 |
| MQTT 클라이언트 | 접속하지 않음(`MqttManager`는 `active=false`로 포인터만 유지) |
| mDNS | `<hostname>.local`, 기본 호스트명 **`broker`** → `broker.local`. `_mqtt._tcp` 서비스도 광고 |
| 자체 발행 | `rv/broker/status` = `online`(retain), `rv/broker/diag/info` = 시스템 정보 JSON (60초) |
| AWS 브리지 | **미구현** (설계서 §3.3). 방향 분리·mTLS는 별도 작업 |

#### retained 메시지 보관
PicoMQTT는 retain 플래그를 전달하기만 하고 **보관하지 않는다.** 설계서 §3.4는 나중에 접속한
디스플레이·HA가 현재 값을 즉시 받는 것을 전제하므로 `EssioBroker`가 직접 보관한다.

- 보관 대상 토픽 패턴: `+/+/state/#`, `+/+/state`, `+/+/status`, `+/+/availability`,
  `+/+/switch/#`, `+/status`, `+/diag/#`, `+/sys/info`, `homeassistant/#`
  → **`cmd`류는 보관하지 않는다.** 재접속 때 예전 명령이 재실행되면 위험하다.
- 한도: `broker.retain_slots`(기본 48) 토픽 × 페이로드 512바이트. 초과분은 `dropped` 카운터로 집계.
- 빈 페이로드는 삭제로 처리한다(HA Discovery 정리 규약과 동일).
- 구독이 걸리면(`on_subscribe`) 일치하는 retained 값을 다시 발행한다.

> **제약** — PicoMQTT는 "특정 클라이언트에만 보내기"를 노출하지 않으므로 재생은 **버스 전체 재발행**이다.
> state류는 현재값이라 중복 수신이 무해(멱등)하지만, 구독자가 많아지면 재접속 때 트래픽이 한 번 튄다.
> `replays` 카운터로 관찰할 수 있다.

### 3.2 Node (`device.role = "node"`)

기존 동작과 같다. 슬롯 0의 드라이버로 장치를 폴링하고 브로커에 발행한다.
보드 1대 = 장치 1대이므로 기본 설정은 슬롯 0만 활성, 슬롯 1~3은 비활성이다.

| 항목 | 내용 |
|---|---|
| 브로커 주소 | §5 해석 순서 |
| mDNS 호스트명 | `rv-node-<device_id>` (설정으로 변경) |
| 물리 버튼·LED | 기본 없음. 조작은 디스플레이 모듈 담당(설계서 §10) |

### 3.3 Display (`device.role = "display"`)

| 항목 | 내용 |
|---|---|
| 하드웨어 | Elecrow CrowPanel 2.1" HMI Rotary Display (ESP32-S3 N16R8, 480×480 원형 ST7701 RGB, CST8xx 터치, 노브). `display.panel = "crowpanel_2_1"` |
| 빌드 | `esp32s3` env만 `-DESSIO_DISPLAY=1`로 LVGL 9.1 + Arduino_GFX 1.6.7을 넣는다. `esp32dev` 빌드에서는 모드 목록에 `supported:false`로 나오고 고를 수 없다 |
| Wi-Fi·MQTT | Node와 같다(라우터 → 브로커 AP → 설정용 AP). MQTT 클라이언트로 브로커에 접속, base topic `rv/display-<id>` |
| 구독 | `<root>/+/{meta,state,availability}`, `<root>/+/switch/+/state` 와 슬롯 2개 이상 노드용 `<root>/+/+/…`. `<root>` = `display.topic_root`(기본 `rv`) |
| 페이지 | 홈 → BMS → 인버터 → 스위치 → 기타. 장치가 도착한 순서와 무관하게 meta의 `type`으로 정렬한다 |
| 홈 화면 | 제목 573PT, Wi-Fi·MQTT 상태, 배터리 요약(BMS의 SOC와 충·방전 W — 인버터와 같은 배터리라 장치 이름 없이), 바로가기 스위치 3×2(`display.shortcuts`, 기본 Inverter·Mover·Pump·Lights·Drain·Fill). 바로가기는 표시명 또는 토픽 이름이 같은 스위치(대소문자 무시)를 모든 장치에서 찾아 묶고, 없거나 장치가 오프라인이면 비활성. 테두리는 BMS와 같은 배터리 링 |
| 테두리 그래프 | 5°마다 점 하나로 그린다(`lv_arc`는 지름 460px에서 프레임당 1초 넘게 걸려 워치독이 걸렸다). meta `ring`으로 종류를 정한다 — `battery:<soc>,<전력>`: 왼쪽 반원 SOC(청록, 아래→위), 오른쪽 반원 충·방전량(보라, 3시에서 충전은 아래로·방전은 위로, ±3kW에서 가득). `flows:<a>,<b>,<c>`: 왼쪽 태양광(노랑)·오른쪽 위 그리드(파랑)·아래 인버터 출력(주황), 각 3kW에서 가득. 해당 값 글자도 같은 색 |
| 스위치 확인 | 모든 스위치 버튼(바로가기 포함)은 누르면 "Turn ON? / Turn OFF?" 확인 창을 띄우고 ON/OFF를 눌러야 명령을 보낸다. 노브를 누르거나 돌리면 취소, 10초 뒤 저절로 닫힌다 |
| 조작 | 노브 돌림 = 페이지 이동(홈 ↔ 장치별), 노브 누름 = 홈. 스와이프도 된다. 스위치 버튼 터치 → `<prefix>/switch/<name>/set` 에 `ON`/`OFF` 발행. 버튼 색은 노드가 다시 보내는 상태 토픽으로만 바뀐다 |
| 화면 끄기 | `display.dim_after_s`(기본 60초) 동안 입력이 없으면 `dim_brightness`로 어둡게. 어두운 상태의 첫 터치·노브는 깨우기만 하고 버튼을 누르지 않는다 |
| 장치 폴링 | 없음. 이 보드는 GPIO 대부분을 패널이 쓰므로 슬롯·포트를 만들지 않는다 |
| 화면 개발 | `PLATFORMIO_BUILD_FLAGS=-DESSIO_DISPLAY_DEMO`로 빌드하면 가짜 장치 3개(UPower·JBD·릴레이)가 들어간다 |

#### `<prefix>/meta` (노드 → 디스플레이, retain)
노드는 접속·Rediscover 때 슬롯마다 장치 설명을 발행한다. 디스플레이는 이것만 보고 화면을 만들므로
드라이버를 새로 추가해도 디스플레이 코드는 고칠 필요가 없다.

```json
{"type":"jbdbms","label":"BMS","ring":"battery:soc,power",
 "switches":[{"n":"charge_fet","l":"Charge MOSFET"}, …],
 "metrics":[{"l":"SOC","u":"%","p":"soc"},{"l":"Power","u":"W","p":"power"}, …]}
```
- `metrics`는 드라이버의 `keySensors()`(쉼표 구분 센서 키) 순서. 첫 번째가 대표값이고, 단위가 `%`면 화면 테두리 링으로도 보인다.
- `p`는 state JSON 안의 점 경로. meta가 없는 노드(구버전)는 state 최상위 숫자 값과 스위치 상태 토픽으로 대신 그린다.
- `ring`은 드라이버의 `displayRing()` (UPower `flows:pv.chg_w,grid.in_w,inv.out_w`, JBD `battery:soc,power`). 없으면 대표값이 %일 때 한 줄 링만 그린다.
- 브로커는 `+/+/meta`, `+/+/+/meta`를 retained로 보관한다.
- 메모리: LVGL 객체는 PSRAM(`src/display/LvglMem.cpp`), 그리기 버퍼 2장(20줄)은 내부 RAM. 내부 RAM이 30KB대로 떨어지면 TCP 송신이 실패해 MQTT가 끊겼다. 프레임이 150ms를 넘으면 `display: frame took …` 경고가 남는다.
- 시험: `tools/mqtt_sim.py`가 BMS·인버터·릴레이 가짜 노드를 발행하고 스위치 명령에 응답한다.

### 3.4 MACH (더미)
프로토콜이 확인되지 않아 **수동 스니퍼**로만 동작한다. 요청을 보내지 않고 수신 바이트를 20ms 공백 기준으로
프레임으로 끊어 `state.last_hex`·`frames`·`rx_bytes`에 담고 debug 로그로 남긴다. 5초 동안 아무것도 안 오면
오류(오프라인 판정용). 실제 장치와 기존 컨트롤러 사이 선로에 RX만 물려 두고 보레이트를 맞춰 가며 패킷을 모은다.

## 4. 토픽 접두 기본값

설계서 §3.4의 `rv/<node>/...`에 맞춘다.

| 상황 | `base_topic` 기본값 | 슬롯 토픽 |
|---|---|---|
| Broker | `rv/broker` | — |
| Node, 활성 슬롯 1개 | `rv/<슬롯 slug>` (예: `rv/upower`) | `rv/upower/state`, `rv/upower/switch/inverter/set` |
| Node, 활성 슬롯 2개 이상 | `rv/<첫 슬롯 slug>` | `<base>/<slug>/state` — slug 구간이 추가된다 |
| 명시 설정 | `mqtt.base_topic` 값 그대로 | |

활성 슬롯이 하나면 slug 구간을 생략해 `rv/upower/upower/state` 같은 중복을 피한다
(`Scheduler::slotPrefix()`).

state·switch·availability·status는 모두 `retain=true`로 발행한다.

> 아직 설계서와 다른 점: 설계서는 `rv/<node>/state/<metric>`처럼 **메트릭당 개별 토픽**을 쓰지만
> 현재 구현은 `rv/<node>/state`에 JSON 한 덩어리를 싣고 HA `value_template`으로 뽑아 쓴다.
> 개별 토픽 전환은 별도 작업으로 남겨둔다.

## 5. 노드의 브로커 주소 해석 (설계서 §11.6)

```
1. 게이트웨이 브로커 AP(wifi.fallback_ssid)에 붙어 있으면 그 게이트웨이 = 브로커
2. 비콘       최근 30초 안에 받은 브로커 UDP 브로드캐스트 (포트 47300, "essio-broker <hostname> <port>")
3. mDNS       mqtt.mdns_name (기본 "broker") → broker.local 조회, 2초 타임아웃
4. 마지막 성공 NVS("essio"/"broker_ip")에 "<SSID>\n<주소>"로 저장. 지금 붙은 SSID와 같을 때만 쓴다
5. 수동 입력   mqtt.host
```

- 접속에 성공하면 그 주소를 SSID와 함께 NVS에 기록한다. 예전에는 망 구분 없이 저장해, 브로커 AP에서 얻은
  `192.168.4.1`을 라우터 망에서도 계속 붙잡고 실패했다.
- 브로커는 붙은 망마다(라우터 STA, 자기 AP) 5초마다 비콘을 브로드캐스트한다. 무선 단말 사이 멀티캐스트를
  제대로 넘기지 않는 공유기가 흔해 mDNS만으로는 못 찾는 경우가 있다. 비콘이 오면 재시도 대기 없이 바로 접속한다.
- `mqtt.mdns_name`을 **비우면** 1~3(자동 탐색)을 건너뛰고 저장 주소·수동 주소만 쓴다.
- 모든 역할에서 Wi-Fi 절전(modem sleep)을 끈다. 켜 두면 DTIM 사이에 무선이 잠들어 멀티캐스트·브로드캐스트를 놓친다.
- **공유기의 무선 격리(AP/client isolation)가 켜져 있으면 어떤 방법으로도 보드끼리 통신할 수 없다.**
  PC에서는 두 보드에 다 접속되는데 보드끼리는 unicast(수동 주소)도 `rc=-2`로 실패하면 이 경우다.
  공유기에서 격리를 끄거나, 라우터 없이 브로커 AP(`RV-FALLBACK`)로 묶는다.
- 현재 어떤 경로로 붙었는지는 `GET /api/system/info`의 `mqtt_address_source`
  (`gateway` / `beacon` / `mdns` / `last_good` / `manual` / `none`)로 확인한다.
- 브로커는 자기 AP를 띄울 때도 mDNS를 켠다(예전에는 라우터 접속 때만 켜서 AP 위에서는 `broker.local`이 안 찾혔다).

## 6. Wi-Fi 모드 (설계서 §3.1, §3.2)

### Broker
```
부팅 → wifi.ssid 스캔 (채널당 400ms ≈ 5초)
 ├─ 발견  → STA 단독 (AWS 브리지 자리)
 └─ 없음  → SoftAP 단독, SSID = wifi.fallback_ssid (기본 RV-FALLBACK)
STA 끊김이 wifi.ap.fallback_after_s 지속 → SoftAP 단독으로 내려감
SoftAP 상태에서 30초 주기로 라우터 재탐색 → 보이면 STA로 승격
```
APSTA 상시 동작을 피한다(AP·STA가 무선 칩과 채널을 공유해 처리량·지연이 같이 나빠짐).

### Node
```
1순위 wifi.ssid (라우터) → 2순위 wifi.fallback_ssid (브로커 SoftAP)
후보당 15초씩 번갈아 시도
둘 다 실패가 wifi.ap.fallback_after_s 지속 → 설정용 SoftAP(RV-SETUP-<id>)를 함께 기동
```

## 7. 설정 항목

`docs/12-config-schema.md` 참조. 이번에 추가된 것:

| 키 | 기본값 | 용도 |
|---|---|---|
| `device.role` | `node` | `node` \| `broker` |
| `wifi.fallback_ssid` / `fallback_password` | `RV-FALLBACK` / `12341234` | 노드의 2순위 망 = 브로커 SoftAP |
| `mqtt.mdns_name` | `broker` | 노드가 찾을 브로커 호스트명. 비우면 mDNS 생략 |
| `broker.port` | 1883 | 브로커 수신 포트 |
| `broker.username` / `password` | 빈 값 | 설정하면 인증 강제(둘 다 필요) |
| `broker.max_clients` | 8 | 초과 접속 거부 |
| `broker.retain_slots` | 48 | retained 토픽 보관 수 |

## 8. 남은 작업

- AWS 브리지 (설계서 §3.3) — 방향 분리, mTLS, 8883
- 메트릭당 개별 state 토픽 전환 (설계서 §3.4)
- 드라이버 전환 시 이전 Discovery 토픽 삭제 (`HaDiscovery::removeSlot`)
- 폴링 3등급 + 변화 시 발행 + 30초 하트비트 (설계서 §11.3)
- MACH 실제 프로토콜 (지금은 스니핑 더미, §3.4) / ANT BMS 드라이버
- 디스플레이: 다른 패널 보드 추가(`DISPLAY_PANELS`), 장치 상세 화면(셀 전압 등)
