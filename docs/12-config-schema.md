# 12. 설정 스키마

파일: LittleFS `/config.json` (#Q-1). 최대 8KB. `schema_version`으로 마이그레이션.

## 1. 전체 예시 (기존 ess.io 구성을 그대로 옮긴 기본값)

```json
{
  "schema_version": 1,
  "device": {
    "name": "ESS Gateway",
    "hostname": "",
    "log_level": "info"
  },
  "wifi": {
    "ssid": "",
    "password": "",
    "static": { "enabled": false, "ip": "", "gateway": "", "subnet": "", "dns": "" },
    "ap": { "ssid": "", "password": "12341234", "fallback_after_s": 60, "keep_when_sta_ok": false }
  },
  "mqtt": {
    "enabled": true,
    "host": "",
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
    { "id": 0, "name": "RS485-A", "kind": "hw1", "rx": 22, "tx": 23, "baud": 115200, "de_pin": -1, "timeout_ms": 500 },
    { "id": 1, "name": "BMS",     "kind": "hw2", "rx": 16, "tx": 17, "baud": 9600,   "de_pin": -1, "timeout_ms": 1000 },
    { "id": 2, "name": "RS485-B", "kind": "sw",  "rx": 18, "tx": 19, "baud": 9600,   "de_pin": -1, "timeout_ms": 500 }
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
    {
      "index": 1, "enabled": true, "type": "jbdbms", "slug": "bms", "label": "JBD BMS",
      "port": 1, "slave_id": 0, "poll_interval_ms": 5000,
      "params": {
        "cell_count": 16,
        "expose_cells": true,
        "expose_protection_bits": true,
        "charge_limit": { "enabled": false, "soc_high": 80, "soc_low": 70 }
      }
    },
    {
      "index": 2, "enabled": true, "type": "rtusw_mk1", "slug": "relay", "label": "Relay Board",
      "port": 2, "slave_id": 255, "poll_interval_ms": 3000,
      "params": {
        "write_retries": 3,
        "channels": [
          { "ch": 1, "name": "Equalizer",      "enabled": true },
          { "ch": 2, "name": "Plumbing Drain", "enabled": true },
          { "ch": 3, "name": "Tank Drain",     "enabled": true },
          { "ch": 4, "name": "Whale to Fill",  "enabled": true },
          { "ch": 5, "name": "Aroundview",     "enabled": true },
          { "ch": 6, "name": "Mover",          "enabled": true },
          { "ch": 7, "name": "12v Charger",    "enabled": true },
          { "ch": 8, "name": "Channel 8",      "enabled": false }
        ]
      }
    },
    { "index": 3, "enabled": false, "type": "none" }
  ],
  "io": {
    "buttons": [
      { "pin": 14, "active_low": true, "debounce_ms": 50,
        "action": { "slot": 2, "switch": "ch6", "mode": "toggle" },
        "long_press_ms": 0, "long_action": null },
      { "pin": 12, "active_low": true, "debounce_ms": 50,
        "action": { "slot": 0, "switch": "inverter", "mode": "toggle" },
        "long_press_ms": 0, "long_action": null }
    ],
    "outputs": [
      { "pin": 26, "active_high": true, "source": { "slot": 2, "switch": "ch6" } },
      { "pin": 25, "active_high": true, "source": { "slot": 0, "switch": "inverter" } }
    ]
  }
}
```

## 2. 필드 정의

### `device`
| 필드 | 타입 | 기본 | 검증 |
|---|---|---|---|
| name | string ≤32 | "ESS Gateway" | HA device name |
| hostname | string ≤32 | "" → `essio-<device_id>` | `[a-z0-9-]` |
| log_level | enum error/warn/info/debug | info | |

### `wifi`
| 필드 | 타입 | 기본 | 비고 |
|---|---|---|---|
| ssid | ≤32 | "" | 빈 값 = AP 전용 |
| password | ≤64 | "" | 조회 시 마스킹 |
| static.* | | disabled | IP 형식 검증 |
| ap.ssid | ≤32 | "" → `essio-<device_id>` | |
| ap.password | 8..63 | "12341234" | |
| ap.fallback_after_s | 10..600 | 60 | |
| ap.keep_when_sta_ok | bool | false | |

### `mqtt`
| 필드 | 타입 | 기본 | 비고 |
|---|---|---|---|
| enabled | bool | true | |
| host | ≤64 | "" | 빈 값 = DISABLED |
| port | 1..65535 | 1883 | |
| username/password | ≤64 | "" | 둘 다 빈 값이면 익명 |
| client_id_suffix | ≤16 | "" | client_id = `essio-<device_id><suffix>` |
| base_topic | ≤64 | "" → `essio/<device_id>` | 선행/후행 `/` 불가, `#`,`+` 불가 |
| keepalive_s | 10..300 | 30 | |
| discovery.enabled | bool | true | |
| discovery.prefix | ≤32 | homeassistant | |
| publish_min_interval_ms | 0..60000 | 1000 | 동일 슬롯 상태 재발행 최소 간격 |
| legacy_topics | bool | false | #Q-5 |

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
