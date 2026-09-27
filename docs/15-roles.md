# 15. 동작 모드 (역할 선택)

하나의 펌웨어가 **브로커 호스트** 또는 **장치 클라이언트** 중 하나로 동작한다.
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
1. mDNS       mqtt.mdns_name (기본 "broker") → broker.local 조회, 2초 타임아웃
2. 마지막 성공 NVS("essio"/"broker_ip")에 저장된 주소
3. 수동 입력   mqtt.host
```

- 접속에 성공하면 그 주소를 NVS에 기록한다.
- `mqtt.mdns_name`을 **비우면** mDNS를 건너뛰므로 수동 주소만 쓰게 된다. 라우터가 mDNS를 막을 때의 탈출구.
- 현재 어떤 경로로 붙었는지는 `GET /api/system/info`의 `mqtt_address_source`
  (`mdns` / `last_good` / `manual` / `none`)로 확인한다.

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
- MACH / ANT BMS 드라이버 (MACH는 최후순위)
