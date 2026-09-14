# 05. 장치 모듈: RTU 릴레이 스위치 보드 Mk1 (RtuSwMk1)

> 소스: `ess.io/src/rtusw_mk1.h`, `rtusw_mk1.cpp`. 도메인 이름 `rtusw_mk1`.
> 코일(FC01/FC05) 기반 8채널 Modbus RTU 릴레이 보드.

## 1. 통신

| 항목 | 값 |
|---|---|
| 프로토콜 | Modbus RTU, 9600 8N1 |
| 기존 포트 | SoftwareSerial RX18 / TX19 (`serialSwitch`, `modbusSwitch`) |
| 실제 송신 슬레이브 주소 | **255 (0xFF)** — `main.cpp`의 `modbusSwitch.begin(255, ...)`. 다수의 저가 릴레이 보드가 0xFF를 "모든 주소 응답"으로 취급 |
| 논리 슬레이브 ID | 생성자 `_slaveID` = 0 — **토픽 이름과 명령 라우팅에만 사용**, Modbus 프레임에는 반영되지 않음(`prepareModbus` 미호출) |
| 채널 수 | 생성자 `_numOfSW` (기존 8), 배열 크기 `NUM_RTU_MK1_SW + 1 = 9` (1-based) |
| 채널 이름 | 가변 인자 `role[1..numOfSW]` |

기존 인스턴스 채널 이름:

| ch | role | 비고 |
|---|---|---|
| 1 | Equalizer | |
| 2 | Plumbing Drain | |
| 3 | Tank Drain | |
| 4 | Whale to Fill | |
| 5 | Aroundview | |
| 6 | Mover | 물리 버튼(GPIO14) 및 상태 핀(GPIO26) 연동 |
| 7 | 12v Charger | |
| 8 | (미전달 — 미초기화 포인터) | |

## 2. Modbus 맵

### 2.1 읽기 — FC01 Read Coils
- 요청: `readCoils(0x0000, 8)`
- 응답 byte0: bit(j−1) = 채널 j 상태 (j = 1..8, LSB = 채널 1)
- `isSuccess` 플래그 저장. 실패 시 로그, 상태 유지.

### 2.2 쓰기 — FC05 Write Single Coil
- `writeSingleCoil(coil = ch − 1, ON ? 0xFF : 0x00)` → 와이어 `0xFF00` / `0x0000`
- 코일 주소 0-based (채널 1 = 코일 0).

## 3. 명령 이름 규약 (`change_switch(name, onoff)`)

- `name`은 **두 자리 십진 문자열** `"<slaveID><ch>"` 예: `"06"` = slaveID 0, 채널 6.
- 파싱: `n = atoi(name)`; `dev = n / 10`; `coil = n % 10 − 1`.
- `dev != slaveID` 이면 무시(return 0) — 같은 도메인 토픽을 여러 보드 인스턴스가 공유하는 구조.
- 제약: 채널 ≤ 9, slaveID ≤ 9 에서만 동작. 채널 10 이상은 파싱 불가. 신규에서는 토픽에 `<slot>/<ch>` 구분자 사용 (→ `07-mqtt-homeassistant.md`).

### 3.1 쓰기 재시도 (기존 버그 포함)
```c
while (retVal = modbus->writeSingleCoil(rtuSWNo, sendvalue) == ku8MBSuccess) {
    if (count > 5) break;
    count++;
}
```
- 연산자 우선순위: `retVal = (write(...) == success)` → retVal은 0/1.
- 성공하는 동안 계속 반복(최대 7회 재기록), 첫 실패에서 탈출. `count` 미초기화.
- 이후 `isSuccessRequest = (retVal == ku8MBSuccess)` → `retVal==1`과 `ku8MBSuccess==0` 비교 → 성공했어도 false로 판정될 수 있음.
- 결과 발행: 성공이면 요청값, 실패면 캐시값을 `homeassistant/switch/rtusw_mk1/<dev><ch>/state`에 발행.
- `switch_state[rtuSWNo]` (0-based)에 기록하나 `update_switch`는 1-based → 인덱스 불일치.
→ 신규: 1회 쓰기 + 실패 시 N회 재시도, 성공 시 상태 캐시 즉시 갱신 후 다음 폴링에서 확인.

## 4. MQTT 인터페이스 (기존)

`D = homeassistant/switch/rtusw_mk1`, `<sw>` = `"<slaveID><ch>"` (예: `01`…`08`)

| 용도 | 토픽 | 페이로드 |
|---|---|---|
| 구독 | `D/<sw>/set` | ON / OFF |
| 상태 | `D/<sw>/state` | ON / OFF (retain=false), `isSuccess`일 때만 발행 |
| Discovery | `D/<sw>/config` | 빈 페이로드 → 스위치 JSON (retain) |

Discovery JSON: `uniq_id = "<device_id>_rsw<slaveID><ch>"`, `name = role[ch]`, `state_topic = D/<sw>/state`, `command_topic = D/<sw>/set`.
(`char this_uniq_id[6]`에 `"rsw%d%d"` — slaveID·ch 각 한 자리일 때만 안전.)

`publish_data`, `update_data`는 no-op.

## 5. 물리 버튼 연동 (기존 main.cpp)
- 무버 버튼 → `change_switch("06", !getSwitchState(6))`
- `MOVER_STATE_PIN` ← `getSwitchState(6)`
신규: "버튼 k → 슬롯 s 채널 c" 매핑을 설정으로 (→ `12-config-schema.md` `buttons[]`).
