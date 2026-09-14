# 03. 장치 모듈: UPOWER (EPEVER UPower 하이브리드 인버터/충전 컨트롤러)

> 소스: `ess.io/src/upower.h`, `upower.cpp`. 도메인 이름 `upower` (과거 `epever`).

## 1. 통신

| 항목 | 값 |
|---|---|
| 프로토콜 | Modbus RTU |
| 슬레이브 주소 | 10 (0x0A) |
| 보레이트 | 115200, 8N1 |
| 기존 포트 | SoftwareSerial RX22 / TX23 |
| 읽기 기능코드 | FC04 Read Input Registers (측정값), FC01 Read Coils (스위치) |
| 쓰기 기능코드 | FC05 Write Single Coil (스위치) |
| (비활성) | FC03/FC16 Holding Register 0x960D (스토리지 모드) |

## 2. 데이터 모델

`struct power { float voltage, current, wattage, temp, accumulate, freq; int state, soc; }`

| 인스턴스 | 의미 |
|---|---|
| `pv_in` | 태양광 패널 입력 |
| `pv_charge` | 태양광 → 배터리 충전 출력 |
| `grid_in` | 계통(AC) 입력 (파생값 포함) |
| `grid_charge` | 계통 → 배터리 충전 출력 |
| `inverter_in` | 인버터 DC 입력 |
| `inverter_out` | 인버터 AC 출력 |
| `bypass` | 계통 바이패스(부하 직결) 출력 |
| `battery` | 배터리 |

스위치 4개 (`enum switchType`):

| enum | 값 | 코일 주소 | 토픽 이름 | HA 표시명 | uniq_id 접미 |
|---|---|---|---|---|---|
| `INVERTER` | 0 | 0x0106 | `inverter` | Inverter | `isw` |
| `GRID_PRIO` | 1 | 0x0104 | `gridout_prio` | Grid Output Priority | `bsw` |
| `SOLAR_CHARGE` | 2 | 0x010B | `solar_charge` | Solar Charge | `ssw` |
| `GRID_CHARGE` | 3 | 0x010C | `grid_charge` | Grid Charge | `gsw` |

## 3. 입력 레지스터 맵 (FC04)

기존 코드는 4개 블록을 각각 한 번의 요청으로 읽는다. 아래 "절대 주소" = 블록 시작 + 인덱스.
모든 값은 **unsigned 16비트**이며, 온도만 `int16_t`로 캐스팅. 32비트 값은 `low/100 + high*65536/100`.

### 3.1 블록 A: `0x3500`, 19 레지스터 (0x3500 ~ 0x3512)

| idx | 절대주소 | 필드 | 변환 | 단위 |
|---|---|---|---|---|
| 0 | 0x3500 | `grid_in.voltage` | /100 | V |
| 1–4 | 0x3501–0x3504 | (미사용) | | |
| 5 | 0x3505 | `grid_charge.voltage` | /100 | V |
| 6 | 0x3506 | `grid_charge.current` | /100 | A |
| 7,8 | 0x3507 (L), 0x3508 (H) | `grid_charge.wattage` | (L + H·65536)/100 | W |
| 9–14 | 0x3509–0x350E | (미사용) | | |
| 15,16 | 0x350F (L), 0x3510 (H) | `grid_charge.accumulate` | (L + H·65536)/100 | kWh (HA 표기) |
| 17 | 0x3511 | (미사용) | | |
| 18 | 0x3512 | `grid_charge.temp` | int16 /100 | ℃ |

### 3.2 블록 B: `0x3519`, 20 레지스터 (0x3519 ~ 0x352C)

| idx | 절대주소 | 필드 | 변환 | 단위 |
|---|---|---|---|---|
| 0 | 0x3519 | `pv_in.voltage` | /100 | V |
| 1 | 0x351A | `pv_in.current` | /100 | A |
| 2,3 | 0x351B (L), 0x351C (H) | `pv_in.wattage` | (L + H·65536)/100 | W |
| 4 | 0x351D | `pv_charge.voltage` | /100 | V |
| 5 | 0x351E | `pv_charge.current` | /100 | A |
| 6,7 | 0x351F (L), 0x3520 (H) | `pv_charge.wattage` | (L + H·65536)/100 | W |
| 8–13 | 0x3521–0x3526 | (미사용) | | |
| 14,15 | 0x3527 (L), 0x3528 (H) | `pv_charge.accumulate` | (L + H·65536)/100 | kWh |
| 16 | 0x3529 | `pv_charge.state` | **(reg >> 1) & 0x03** (기존 코드는 `&&` 오타) | 0 No Charging / 1 Float / 2 Boost / 3 Equalization |
| 17,18 | 0x352A–0x352B | (미사용) | | |
| 19 | 0x352C | `pv_charge.temp` | int16 /100 | ℃ |

### 3.3 블록 C: `0x352F`, 13 레지스터 (0x352F ~ 0x353B)

| idx | 절대주소 | 필드 | 변환 | 단위 |
|---|---|---|---|---|
| 0 | 0x352F | `inverter_in.voltage` | /100 | V |
| 1–3 | 0x3530–0x3532 | (미사용) | | |
| 4 | 0x3533 | `inverter_out.voltage` | /100 | V |
| 5 | 0x3534 | `inverter_out.current` | /100 | A |
| 6 | 0x3535 | (미사용) | | |
| 7,8 | 0x3536 (L), 0x3537 (H) | `inverter_out.wattage` | (L + H·65536)/100 | W |
| 9–11 | 0x3538–0x353A | (미사용) | | |
| 12 | 0x353B | `inverter_out.freq` | /100 | Hz |

### 3.4 블록 D: `0x354C`, 16 레지스터 (0x354C ~ 0x355B)

| idx | 절대주소 | 필드 | 변환 | 단위 |
|---|---|---|---|---|
| 0 | 0x354C | `battery.voltage` | /100 | V |
| 1,2 | 0x354D–0x354E | (미사용) | | |
| 3 | 0x354F | `battery.temp` | int16 /100 | ℃ |
| 4 | 0x3550 | `battery.soc` | 그대로 | % (**읽기만 하고 발행 안 함**) |
| 5,6 | 0x3551–0x3552 | (미사용) | | |
| 7 | 0x3553 | `battery.state` | 그대로 | (**읽기만 하고 발행 안 함**) |
| 8–11 | 0x3554–0x3557 | (미사용) | | |
| 12 | 0x3558 | `bypass.voltage` | /100 | V |
| 13 | 0x3559 | `bypass.current` | /100 | A |
| 14,15 | 0x355A (L), 0x355B (H) | `bypass.wattage` | (L + H·65536)/100 (기존 코드는 `;` 오타로 L만 사용) | W |

### 3.5 파생값 (블록 A와 D 모두 성공 시)

```
grid_in.current = grid_charge.current + bypass.current
grid_in.wattage = grid_charge.wattage + bypass.wattage
```

### 3.6 GRID_PRIO 상태에 따른 마스킹 (의도)

```
if (switch_state[GRID_PRIO] == 0) { bypass.voltage = bypass.current = bypass.wattage = 0; }
else                              { inverter_out.voltage = inverter_out.current = inverter_out.wattage = 0; }
```
- 의미: 계통 우선(바이패스) OFF이면 바이패스 출력은 0, ON이면 인버터 출력은 0으로 표기 (실제 하드웨어가 잔류값을 보고하는 것을 억제).
- 기존 코드는 이 블록에 **컴파일 오류** 2건 (세미콜론 누락, 존재하지 않는 멤버 `inverter`). `08-legacy-issues.md` #U-01.
- 신규 구현에서 유지할지 결정 필요 (→ `10-requirements.md` 미결 #Q-3).

### 3.7 읽기 실패 처리

블록별 독립. 실패한 블록의 필드는 **이전 값 유지**(구조체 갱신 안 함). 반환값은 4개 결과 코드의 OR (0 = 전부 성공).

## 4. 코일 (스위치) 읽기/쓰기

### 4.1 읽기 (`update_switch`)
각 코일을 개별 `readCoils(addr, 1)` 4회 요청. 응답 byte0 bit0 = 상태.
실패 시 해당 스위치 캐시 유지.

### 4.2 쓰기 (`change_switch(name, "ON"|"OFF")`)
- `writeSingleCoil(addr, ON ? 0xFF : 0x00)` → ModbusMaster가 `0xFF00`/`0x0000`으로 전송.
- 성공할 때까지 최대 **7회**(try_count 0..6) 시도, 각 시도 사이 `delay(100)`.
- 성공 시 `switch_state[type]` 갱신 + `homeassistant/switch/upower/<name>/state` 에 `"ON"/"OFF"` 즉시 발행.
- 반환값: `result && !mqttResult` (의미 불명확, 호출측에서 사용 안 함).
- 알 수 없는 `name` → `addr=0`, `switch_type=-1` 로 쓰기 시도 후 `switch_state[-1]` 접근 (**메모리 오류**). 신규: 이름 검증 필수.

## 5. 스토리지 모드 (비활성 — 코드 주석 상태, 참고용)

배터리 수명을 위해 충전 상한을 낮추는 기능. 홀딩 레지스터 `0x960D`부터 3워드 쓰기:

| 레지스터 | 의미 | Full 모드 | Storage 모드 |
|---|---|---|---|
| 0x960D | BCV (Boost Charge Voltage) | 16 × 3.65 V = 5840 (0.01V) | 16 × 3.40 = 5440 |
| 0x960E | FCV (Float Charge Voltage) | 16 × 3.45 = 5520 | 16 × 3.30 = 5280 |
| 0x960F | BVR (Boost Voltage Reconnect) | 16 × 3.38 = 5408 | 16 × 3.20 = 5120 |

- 셀 수 계수 `NUM_MAX_CELL = 16`.
- 스위치 이름 `storage`, 토픽 `homeassistant/switch/upower/storage/{config,state,set}`, uniq `stsw`, 표시명 "Storage Mode".
- 상태 판정: `readHoldingRegisters(0x960D,3)` 후 `buf[0] == STORAGE_MODE_BCV`.
- 신규에서 옵션 기능으로 부활 가능 (→ 미결 #Q-4).

## 6. MQTT 인터페이스 (기존 토픽 — 호환성 참고)

### 6.1 구독 (command)
```
homeassistant/switch/upower/inverter/set
homeassistant/switch/upower/solar_charge/set
homeassistant/switch/upower/grid_charge/set
homeassistant/switch/upower/gridout_prio/set
```
페이로드 `"ON"` / `"OFF"`.

### 6.2 스위치 상태 발행 (`publish_switch`, retain=false)
```
homeassistant/switch/upower/inverter/state       ON|OFF
homeassistant/switch/upower/solar_charge/state   ON|OFF
homeassistant/switch/upower/grid_charge/state    ON|OFF
homeassistant/switch/upower/gridout_prio/state   ON|OFF
```

### 6.3 측정값 발행 (`publish_data`, retain=false) — 5개 JSON 토픽

`homeassistant/sensor/upower/grid/state`
```json
{"inVoltage":0.0,"inCurrent":0.0,"inWattage":0.0,"outVoltage":0.0,"outCurrent":0.0,"outWattage":0.0,"temperature":0.0,"accumulate":0.0}
```
`homeassistant/sensor/upower/pv/state`
```json
{"inVoltage":0.0,"inCurrent":0.0,"inWattage":0.0,"outVoltage":0.0,"outCurrent":0.0,"outWattage":0.0,"temperature":0.0,"accumulate":0.0,"state":0}
```
`homeassistant/sensor/upower/inverter/state`
```json
{"inVoltage":0.0,"outVoltage":0.0,"outCurrent":0.0,"outWattage":0.0,"outFrequency":0.0,"temperature":0.0}
```
`homeassistant/sensor/upower/bypass/state`
```json
{"inVoltage":0.0,"inCurrent":0.0,"inWattage":0.0}
```
`homeassistant/sensor/upower/battery/state`
```json
{"outVoltage":0.0,"temperature":0.0}
```
- `inverter.temperature`는 항상 0 (읽는 레지스터 없음).
- `battery.soc`, `battery.state`, `grid_charge.temp`→`grid.temperature`는 발행됨, soc/state는 미발행.

### 6.4 HA Discovery 엔티티 목록 (`setup_entity`)

발행 순서: (1) 아래 모든 config 토픽 + 레거시 `epever/*` config 토픽에 **빈 페이로드**(삭제) → (2) 실제 JSON(retain=true).

**스위치 (4)** — config 토픽 `homeassistant/switch/upower/<name>/config`

| name | uniq_id | 표시명 | state_topic | command_topic |
|---|---|---|---|---|
| inverter | `<id>_isw` | Inverter | `.../upower/inverter/state` | `.../upower/inverter/set` |
| solar_charge | `<id>_ssw` | Solar Charge | `.../upower/solar_charge/state` | `.../set` |
| grid_charge | `<id>_gsw` | Grid Charge | `.../upower/grid_charge/state` | `.../set` |
| gridout_prio | `<id>_bsw` | Grid Output Priority | `.../upower/gridout_prio/state` | `.../set` |

**센서 (25)** — config 토픽 `homeassistant/sensor/upower/<config_name>/config`

| config_name | uniq | 표시명 | unit | device_class(기존) | state_topic (`homeassistant/sensor/upower/`) | value_json |
|---|---|---|---|---|---|---|
| grid_in_current | gic | Grid In Current | A | energy | grid/state | inCurrent |
| grid_in_wattage | giw | Grid In Wattage | W | energy | grid/state | inWattage |
| grid_in_voltage | giv | Grid In Voltage | V | energy | grid/state | inVoltage |
| grid_charge_current | goc | Grid Out Current | A | energy | grid/state | outCurrent |
| grid_charge_wattage | gow | Grid Out Wattage | W | energy | grid/state | outWattage |
| grid_charge_voltage | gov | Grid Out Voltage | V | energy | grid/state | outVoltage |
| grid_charge_accumulate | goa | Grid Out Accumulate | kWh | energy | grid/state | accumulate |
| grid_temperature | got | Grid Temperature | ℃ | temperature | grid/state | temperature |
| pv_in_voltage | piv | PV In Voltage | V | energy | pv/state | inVoltage |
| pv_in_current | pic | PV In Current | A | energy | pv/state | inCurrent |
| pv_in_wattage | piw | PV In Wattage | W | energy | pv/state | inWattage |
| pv_charge_voltage | pov | PV Out Voltage | V | energy | pv/state | outVoltage |
| pv_charge_current | poc | PV Out Current | A | energy | pv/state | outCurrent |
| pv_charge_wattage | pow | PV Out Wattage | W | energy | pv/state | outWattage |
| pv_charge_accumulate | poa | PV Out Accumulate | kWh | energy | pv/state | accumulate |
| pv_temperature | pot | PV Temperature | ℃ | temperature | pv/state | temperature |
| inverter_in_voltage | iiv | Inverter In Voltage | V | energy | inverter/state | inVoltage |
| inverter_out_voltage | iov | Inverter Out Voltage | V | energy | inverter/state | outVoltage |
| inverter_out_current | ioc | Inverter Out Current | A | energy | inverter/state | outCurrent |
| inverter_out_wattage | iow | Inverter Out Wattage | W | energy | inverter/state | outWattage |
| inverter_out_frequency | iof | Inverter Out Frequency | Hz | energy | inverter/state | outFrequency |
| (inverter_temperature — 주석 처리) | iot | Inverter Temperature | ℃ | temperature | inverter/state | temperature |
| bypass_out_voltage | byov | Bypass In Voltage | V | energy | bypass/state | inVoltage |
| bypass_out_current | byoc | Bypass In Current | A | energy | bypass/state | inCurrent |
| bypass_out_wattage | oybw (오타) | Bypass In Wattage | W | energy | bypass/state | inWattage |
| battery_out_voltage | bov | Battery Out Voltage | V | energy | battery/state | outVoltage |
| battery_temperature | bot | Battery Temperature | ℃ | temperature | battery/state | temperature |

- `pv.state`(충전 상태)는 발행되지만 HA 엔티티 미등록.
- device_class `energy`가 V/A/W/Hz 센서에 붙어 있음 (커밋 `b7c4ea4 change device class for statistics` — HA 장기 통계 활성화를 노린 것으로 보이나 규격상 부적절). 신규에서는 `voltage/current/power/frequency/energy(kWh, state_class total_increasing)`로 정정 (→ `07-mqtt-homeassistant.md`).

### 6.5 삭제 대상 레거시 토픽 (빈 페이로드 발행)
`homeassistant/switch/upower/{inverter,grid,solar,bypass}/config`,
`homeassistant/sensor/upower/{utility_charging_current, utility_charging_voltage, utility_charging_wattage, pv_charging_current, pv_charging_voltage, pv_charging_wattage, battery_temp, battery_soc, battery_voltage, inverter_voltage, inverter_current, inverter_wattage, inverter_frequency, bypass_voltage, bypass_current, bypass_wattage, utility_voltage, utility_current, utility_wattage, pv_voltage, pv_current, pv_wattage}/config`,
그리고 동일 목록의 `epever/` 도메인 버전 + `homeassistant/switch/epever/{inverter,grid_charge,solar_charge,gridout_prio}/config`.

신규 구현에서는 "레거시 정리" 기능을 1회성 옵션(웹 UI 버튼)으로 제공하거나 생략 (→ 미결 #Q-5).

## 7. 폴링 비용 (기존)

1 사이클 = FC01 ×4 + FC04 ×4 = Modbus 요청 8회. 115200bps에서 정상 응답 시 수십 ms, 장치 무응답 시 8 × 2000ms = 16초 블로킹.
