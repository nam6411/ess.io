# 07. MQTT / Home Assistant Discovery 규약

이 문서는 (A) 기존 규약 전체를 기록하고, (B) 신규 시스템의 단일 규약을 정의한다.

---

## A. 기존 규약 (ess.io)

### A.1 연결

| 항목 | 값 |
|---|---|
| 클라이언트 ID | `ESP32-<device_id>` |
| 인증 | 사용자명/비밀번호 필수 (빈 값이면 연결 시도 안 함) |
| 버퍼 | 1024 바이트 |
| keepalive / LWT / TLS | 기본값 15s / 없음 / 없음 |
| 재접속 | 폴링 사이클(2초)마다 `setup_mqtt()` 재시도 |
| 발행 시 미접속 | `ESP.restart()` |

### A.2 토픽 네임스페이스
접두 `homeassistant/` (HA Discovery 접두와 상태 토픽을 같은 접두 아래 둠).

| 장치 | 도메인 세그먼트 | config | state | set |
|---|---|---|---|---|
| Upower 스위치 | `switch/upower/<name>` | `/config` | `/state` | `/set` |
| Upower 센서 | `sensor/upower/<cfg>` / `sensor/upower/<group>` | `<cfg>/config` | `<group>/state` (JSON) | — |
| Jbdbms | `sensor|switch/jbdbms_<n>/<entity>` | `/config` | 센서: `sensor/jbdbms_<n>/jbdbms/state` (JSON), 스위치: `/state` | `/set` |
| RtuSwMk1 | `switch/rtusw_mk1/<id><ch>` | `/config` | `/state` | `/set` |
| RtuSwMk2 | `switch/rtusw_mk2/<id><ch>` | `/config` | `/state` | `/set` |
| 시스템 | `switch/reset/set`, `switch/restart/set` | — | — | 구독만 |

### A.3 시스템 명령
- `homeassistant/switch/reset/set` (임의 페이로드) → 모든 장치 Discovery 재발행
- `homeassistant/switch/restart/set` → 재부팅
- 이 두 토픽은 Discovery 등록이 없어 HA UI에는 나타나지 않음(수동 발행용).

### A.4 페이로드 공통
- 스위치 상태/명령: 문자열 `ON` / `OFF`.
- 센서: 그룹별 JSON, `value_template: {{ value_json.<key> }}`.
- retain: Discovery config = true, state = false.

### A.5 Discovery device 블록
`01-legacy-system.md` §10.4 참조. 모든 엔티티가 단일 HA 디바이스 "Battery"에 귀속.

### A.6 엔티티 전체 목록
각 장치 문서 §6 참조 (Upower 4 스위치 + 25 센서, Jbdbms 3 스위치 + 7+2N 센서, RtuSw N 스위치).

---

## B. 신규 규약 (ess.io2)

### B.1 설계 원칙
1. **상태/명령 토픽과 Discovery 토픽 분리**: Discovery는 HA 규약상 `homeassistant/...` 필수. 상태/명령은 사용자 설정 가능한 `base_topic`(기본 `essio/<device_id>`) 아래.
2. **슬롯 기반 네이밍**: 같은 모듈 타입을 여러 개 장착 가능하므로 슬롯 인덱스(또는 사용자 지정 slug)로 구분.
3. **uniq_id 전역 유일**: `<device_id>_s<slot>_<entity>`.
4. **HA 규격 준수**: 올바른 `device_class`, `state_class`, `unit_of_measurement`, `availability`.
5. **LWT**: 브로커가 오프라인을 감지하도록 will 사용.

### B.2 연결

| 항목 | 값 |
|---|---|
| 클라이언트 ID | `essio-<device_id>` |
| 인증 | 사용자명/비밀번호 선택(빈 값 허용 → 익명) |
| keepalive | 30s |
| LWT | `<base>/status` = `offline`, retain |
| 접속 후 | `<base>/status` = `online`, retain |
| 버퍼 | 2048 (Discovery JSON이 device 블록 포함 ~600B) |
| 재접속 | 지수 백오프 1s→2s→…→30s, 비블로킹 |
| TLS | 1차 범위 외 (미결 #Q-8) |

### B.3 토픽 구조

```
<base> = <base_topic>            (기본 essio/<device_id>)
<slot> = s0 | s1 | s2 | s3        (또는 설정된 slug)

<base>/status                                   online|offline (LWT)
<base>/sys/info                                 JSON {ip, rssi, uptime, heap, fw, slots:[...]}   (60s 주기)
<base>/sys/cmd                                  구독: restart | rediscover | factory_reset
<base>/<slot>/state                             JSON 측정값 (모듈별 스키마)
<base>/<slot>/switch/<name>/state               ON|OFF
<base>/<slot>/switch/<name>/set                 구독: ON|OFF
<base>/<slot>/availability                      online|offline  (모듈 연속 오류 시 offline)

homeassistant/sensor/<device_id>_<slot>/<entity>/config     Discovery (retain)
homeassistant/switch/<device_id>_<slot>/<entity>/config
homeassistant/binary_sensor/<device_id>_<slot>/<entity>/config
```

### B.4 Discovery payload 표준 형식

```json
{
  "uniq_id": "<device_id>_<slot>_<entity>",
  "obj_id":  "<slot_slug>_<entity>",
  "name": "<표시명>",
  "stat_t": "<base>/<slot>/state",
  "val_tpl": "{{ value_json.<entity> }}",
  "unit_of_meas": "V",
  "dev_cla": "voltage",
  "stat_cla": "measurement",
  "avty": [ {"t": "<base>/status"}, {"t": "<base>/<slot>/availability"} ],
  "avty_mode": "all",
  "dev": {
    "ids": ["<device_id>"],
    "cns": [["mac", "<mac>"]],
    "name": "<config.device_name>",
    "mf": "nam6411",
    "mdl": "ess.io2",
    "sw": "<fw_version>",
    "cu": "http://<ip>/"
  }
}
```
- 스위치: `cmd_t`, `stat_t = <base>/<slot>/switch/<name>/state`, `pl_on/pl_off = ON/OFF`.
- 누적 에너지(kWh): `dev_cla: energy`, `stat_cla: total_increasing`.
- 진단 정보(rssi, uptime): `ent_cat: diagnostic`.

### B.5 모듈별 엔티티 매핑 (신규)

#### Upower (`<base>/<slot>/state` JSON)
```json
{"pv":{"in_v":0,"in_a":0,"in_w":0,"chg_v":0,"chg_a":0,"chg_w":0,"kwh":0,"temp":0,"state":0},
 "grid":{"in_v":0,"in_a":0,"in_w":0,"chg_v":0,"chg_a":0,"chg_w":0,"kwh":0,"temp":0},
 "inv":{"in_v":0,"out_v":0,"out_a":0,"out_w":0,"hz":0},
 "bypass":{"v":0,"a":0,"w":0},
 "bat":{"v":0,"temp":0,"soc":0,"state":0}}
```
value_template은 중첩 키 `{{ value_json.pv.in_v }}`. 엔티티 29개(기존 25 + bat.soc, bat.state, pv.state, grid.temp 정리). 스위치 4개(`inverter, gridout_prio, solar_charge, grid_charge`) + 옵션 `storage_mode`.

device_class 정정: `_v`→voltage/V, `_a`→current/A, `_w`→power/W, `kwh`→energy/kWh total_increasing, `temp`→temperature/°C, `hz`→frequency/Hz, `soc`→battery/%, `state`→enum(sensor, `options`).

#### Jbdbms
```json
{"pack_v":0,"current":0,"power":0,"remain_ah":0,"full_ah":0,"soc":0,"cycles":0,
 "cell_diff":0,"cell_v":[...],"ntc":[...],"chg_fet":true,"dis_fet":true,
 "protection":0,"balance":0}
```
센서: pack_v, current, power, remain_ah, full_ah, soc, cycles, cell_diff, cell_v[i] (`{{ value_json.cell_v[i] }}`), ntc[i]. binary_sensor: protection 각 비트(옵션). 스위치: `charge_fet`, `discharge_fet`. 옵션 스위치 `charge_limit`(→ F-JBD-5).

#### RtuSwMk1 / Mk2
`<base>/<slot>/switch/ch<n>/state|set` (n = 1..N). 표시명 = 설정된 채널 이름. `<base>/<slot>/state` = `{"ch":[true,false,...]}` (진단용).

### B.6 호환 모드 (선택 옵션 `mqtt.legacy_topics = true`)
기존 HA 자동화가 옛 토픽(`homeassistant/switch/upower/inverter/set` 등)에 의존하는 경우를 위해, 슬롯별로 "레거시 도메인" 문자열을 설정하면 A.2 규약으로 **추가** 발행/구독한다. 1차 구현 범위 여부는 미결 #Q-5.
