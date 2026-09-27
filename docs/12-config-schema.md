# 12. 설정 스키마

파일: LittleFS `/config.json` (#Q-1). 최대 8KB. `schema_version`으로 마이그레이션.

## 1. 전체 예시 (기존 ess.io 구성을 그대로 옮긴 기본값)

```json
{
  "schema_version": 1,
  "device": {
    "role": "node",
    "name": "ESS Gateway",
    "hostname": "",
    "log_level": "info"
  },
  "wifi": {
    "ssid": "",
    "password": "",
    "fallback_ssid": "RV-FALLBACK",
    "fallback_password": "12341234",
    "static": { "enabled": false, "ip": "", "gateway": "", "subnet": "", "dns": "" },
    "ap": { "ssid": "", "password": "12341234", "fallback_after_s": 60, "keep_when_sta_ok": false }
  },
  "broker": {
    "port": 1883,
    "username": "",
    "password": "",
    "max_clients": 8,
    "retain_slots": 48
  },
  "mqtt": {
    "enabled": true,
    "host": "",
    "mdns_name": "broker",
    "port": 1883,
    "username": "",
    "password": "",
    "client_id_suffix": "",
    "base_topic": "",
    "keepalive_s": 30,
    "discovery": { "enabled": true, "prefix": "homeassistant" },
    "publish_min_interval_ms": 1000,
    "legacy_topics": false
  },
  "web": {
    "auth": { "enabled": false, "username": "admin", "password": "" }
  },
  "ports": [
    { "id": 0, "name": "RS485",   "kind": "hw1",  "rx": 18, "tx": 17, "baud": 115200, "de_pin": -1, "timeout_ms": 500 },
    { "id": 1, "name": "RS485-B", "kind": "none", "rx": 16, "tx": 15, "baud": 9600,   "de_pin": -1, "timeout_ms": 1000 },
    { "id": 2, "name": "RS485-C", "kind": "none", "rx": -1, "tx": -1, "baud": 9600,   "de_pin": -1, "timeout_ms": 500 }
  ],
  "slots": [
    {
      "index": 0, "enabled": true, "type": "upower", "slug": "upower", "label": "UPower",
      "port": 0, "slave_id": 10, "poll_interval_ms": 5000,
      "params": {
        "blocks": { "grid": true, "pv": true, "inverter": true, "battery": true },
        "write_retries": 3,
        "mask_by_grid_prio": false,
        "storage_mode": { "enabled": false, "cells": 16, "bcv_mv": 3650, "fcv_mv": 3450, "bvr_mv": 3380,
                          "storage_bcv_mv": 3400, "storage_fcv_mv": 3300, "storage_bvr_mv": 3200 }
      }
    },
    { "index": 1, "enabled": false, "type": "none" },
    { "index": 2, "enabled": false, "type": "none" },
    { "index": 3, "enabled": false, "type": "none" }
  ],
  "io": { "buttons": [], "outputs": [] }
}
```

## 2. 필드 정의

기본 구성은 **노드 역할 + 슬롯 0에 장치 하나**다(설계서 §2: 보드 1대 = 장치 1대).
브로커로 쓰려면 `device.role`을 `broker`로 바꾼다 — 자세한 절차와 동작 차이는
[15-roles.md](15-roles.md).

### `device`
| 필드 | 타입 | 기본 | 검증 |
|---|---|---|---|
| role | enum node/broker | node | 변경 시 재부팅 필요 (→ [15-roles.md](15-roles.md) §2) |
| name | string ≤32 | "ESS Gateway" | HA device name |
| hostname | string ≤32 | "" → `essio-<device_id>` | `[a-z0-9-]` |
| log_level | enum error/warn/info/debug | info | |

### `wifi`
| 필드 | 타입 | 기본 | 비고 |
|---|---|---|---|
| ssid | ≤32 | "" | 1순위(라우터). 빈 값이면 fallback만 시도 |
| password | ≤64 | "" | 조회 시 마스킹 |
| fallback_ssid | ≤32 | `RV-FALLBACK` | 2순위 = 브로커 SoftAP. 브로커 역할에서는 자기 AP 이름으로도 쓰인다 |
| fallback_password | ≤64 | `12341234` | 조회 시 마스킹 |
| static.* | | disabled | IP 형식 검증 |
| ap.ssid | ≤32 | "" → `essio-<device_id>` | |
| ap.password | 8..63 | "12341234" | |
| ap.fallback_after_s | 10..600 | 60 | |
| ap.keep_when_sta_ok | bool | false | |

### `mqtt`
| 필드 | 타입 | 기본 | 비고 |
|---|---|---|---|
| enabled | bool | true | Node 역할에서만 의미 있음 |
| host | ≤64 | "" | 수동 주소(해석 순서의 마지막) |
| mdns_name | ≤32 | `broker` | `<이름>.local` 탐색. 비우면 mDNS 생략 → 수동 주소만 사용 |
| port | 1..65535 | 1883 | |
| username/password | ≤64 | "" | 둘 다 빈 값이면 익명 |
| client_id_suffix | ≤16 | "" | client_id = `essio-<device_id><suffix>` |
| base_topic | ≤64 | "" → `essio/<device_id>` | 선행/후행 `/` 불가, `#`,`+` 불가 |
| keepalive_s | 10..300 | 30 | |
| discovery.enabled | bool | true | |
| discovery.prefix | ≤32 | homeassistant | |
| publish_min_interval_ms | 0..60000 | 1000 | 동일 슬롯 상태 재발행 최소 간격 |
| legacy_topics | bool | false | #Q-5 |

### `broker` (Broker 역할에서만 사용)
| 필드 | 타입 | 기본 | 검증 |
|---|---|---|---|
| port | 1..65535 | 1883 | 수신 포트 |
| username | ≤64 | "" | 설정하면 인증 강제 |
| password | ≤64 | "" | username이 있으면 필수. 조회 시 마스킹 |
| max_clients | 1..16 | 8 | 초과 접속은 `CRC_SERVER_UNAVAILABLE`로 거부. SoftAP 최대 접속 수에도 쓰인다 |
| retain_slots | 0..256 | 48 | retained 토픽 보관 수 (→ [15-roles.md](15-roles.md) §3.1) |

### `web.auth`
| 필드 | 기본 |
|---|---|
| enabled | false |
| username | admin |
| password | "" (enabled=true 이면 필수) |

### `ports[]` (정확히 3개, id 0..2)
| 필드 | 타입 | 검증 |
|---|---|---|
| kind | hw1 / hw2 / sw / none | hw1·hw2는 각각 1회만; `sw`는 baud ≤ 57600 권장(115200 시 경고) |
| rx, tx | GPIO | 유효 핀, 서로·다른 포트·IO 핀과 중복 불가, 입력 전용 핀(34~39)은 tx 불가 |
| baud | 1200..115200 | |
| de_pin | -1 또는 GPIO | |
| timeout_ms | 100..3000 | Modbus/시리얼 응답 대기 |

### `slots[]` (정확히 4개, index 0..3)
| 필드 | 타입 | 검증 |
|---|---|---|
| enabled | bool | |
| type | none / upower / jbdbms / rtusw_mk1 / rtusw_mk2 | |
| slug | `[a-z0-9_]{1,16}` | 슬롯 간 유일; 토픽·object_id에 사용 |
| label | ≤32 | HA 표시명 접두 |
| port | 0..2 | 해당 포트 kind ≠ none |
| slave_id | 0..255 | |
| poll_interval_ms | 1000..600000 | |
| params | 타입별 객체 | 아래 |

#### `params` — upower
| 필드 | 기본 |
|---|---|
| blocks.grid/pv/inverter/battery | true |
| write_retries | 3 (0..10) |
| mask_by_grid_prio | false |
| storage_mode.enabled | false; 이하 mV 단위 셀 전압 및 셀 수 |

#### `params` — jbdbms
| 필드 | 기본 |
|---|---|
| cell_count | 16 (1..32) |
| ntc_count | 2 (0..8) — Discovery에 등록할 NTC 센서 수 (응답의 실제 개수와 무관) |
| expose_cells | true |
| expose_protection_bits | true |
| charge_limit.enabled / soc_high / soc_low | false / 80 / 70 (`soc_low < soc_high`) |

#### `params` — rtusw_mk1 / rtusw_mk2
| 필드 | 검증 |
|---|---|
| write_retries | 0..10 |
| channels[] | Mk1 1..8개, Mk2 1..4개; `ch` 유일; `name` ≤32 |

### `io`
| 필드 | 검증 |
|---|---|
| buttons[] ≤4 | pin 유일, 포트 핀과 중복 불가, `action.slot` 활성 슬롯, `action.switch` 해당 모듈 스위치 이름 |
| outputs[] ≤4 | 동일 |

## 3. 마스킹 규칙
`GET /api/config` 응답에서 `wifi.password`, `mqtt.password`, `web.auth.password`는 값이 있으면 `"********"`. `PUT`에서 `"********"`가 오면 기존 값 유지.

## 4. 적용 범위 (변경 → 동작)
| 변경 키 | 동작 |
|---|---|
| device.role | **재부팅** — 브로커·스케줄러·UART 소유권이 통째로 바뀜 |
| broker.* | 브로커 재기동 (Broker 역할에서만) |
| device.hostname | 재부팅 필요 (`restart_required: true`) |
| wifi.* | STA 재접속 |
| mqtt.* | MQTT 재접속 + Discovery 재발행 |
| ports[i] | 해당 포트 재초기화 + 그 포트의 슬롯 재초기화 |
| slots[i] | 슬롯 i 재초기화, type 변경 시 이전 Discovery 삭제 |
| io.* | IoManager 재설정 |
| web.auth | 즉시 |

## 5. 마이그레이션
- `schema_version` 없음/0 → 기본값으로 생성.
- 기존 ess.io EEPROM 값 가져오기는 지원하지 않음(다른 저장소). 웹 UI 입력으로 재설정.
