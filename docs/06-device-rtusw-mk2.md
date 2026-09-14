# 06. 장치 모듈: RTU 릴레이 스위치 보드 Mk2 (RtuSwMk2)

> 소스: `ess.io/src/rtusw_mk2.h`, `rtusw_mk2.cpp`. 도메인 이름 `rtusw_mk2`.
> 홀딩 레지스터(FC03/FC06) 기반 4채널 Modbus RTU 릴레이 보드. 기존 `main.cpp`에서는 인스턴스화되지 않았으나(코드는 완성) 신규 범위에 포함.

## 1. 통신

| 항목 | 값 |
|---|---|
| 프로토콜 | Modbus RTU, 9600 8N1 |
| 슬레이브 주소 | 생성자 `_slaveID` (Modbus 프레임 주소는 공유 `ModbusMaster`의 설정을 따름 — Mk1과 동일한 구조적 한계) |
| 채널 수 | 생성자 `_numOfSW` (최대 `NUM_RTU_MK2_SW = 4`), 배열 크기 5 (1-based) |
| 채널 이름 | 가변 인자 `role[1..numOfSW]` |

## 2. Modbus 맵

### 2.1 읽기 — FC03 Read Holding Registers
- 요청: `readHoldingRegisters(0x0001, 4)`
- 레지스터 `0x0001 + j` (j=0..3) ≠ 0 → 채널 j+1 ON. (실제 보드는 보통 `0x0001` = 열림, `0x0002` = 닫힘 등을 반환하나 기존 코드는 비0 여부만 판정)
- `isSuccess` 저장.

### 2.2 쓰기 — FC06 Write Single Register
- 레지스터 주소 = 채널 번호 (1-based, `0x0001`~`0x0004`)
- 값: **ON = `0x0100`**, **OFF = `0x0200`**
  (일반적인 "R4D3B16 / N4D8B08 계열" 릴레이 보드 규약: 0x0100 open, 0x0200 close, 0x0300 toggle, 0x0400 latch, 0x0500 momentary, 0x0600 delay)
- 신규 확장 후보: `0x0300` 토글, `0x0600` + 지연시간(하위 바이트, 초) 모멘터리 (→ 미결 #Q-7).

## 3. 명령 이름 규약 (`change_switch`)

- `name = "<slaveID><ch>"`, `n = atoi(name)`, `dev = n / 10`, `reg = n % 10` (**Mk1과 달리 −1 없음**, 레지스터 주소가 1-based이므로 정합)
- `dev != slaveID` → 무시.
- 재시도 루프는 Mk1과 동일한 버그 패턴 (`while (retVal = write(...) == success)`, count 미초기화).
- 결과 상태 발행: `homeassistant/switch/rtusw_mk2/<dev><reg>/state`.

## 4. MQTT 인터페이스 (기존)

`D = homeassistant/switch/rtusw_mk2`, `<sw>` = `"<slaveID><ch>"`

| 용도 | 토픽 | 페이로드 |
|---|---|---|
| 구독 | `D/<sw>/set` | ON / OFF |
| 상태 | `D/<sw>/state` | ON / OFF, `isSuccess`일 때만 |
| Discovery | `D/<sw>/config` | 빈 페이로드 → 스위치 JSON (retain) |

Discovery JSON: `uniq_id = "<device_id>_rsw<slaveID><ch>"` (**Mk1과 접두 `rsw` 동일 → slaveID·ch가 같으면 uniq_id 충돌**), `name = role[ch]`.

`publish_data`, `update_data`는 no-op. `update_switch`는 `isSuccess` 반환(Mk1은 0 반환).

## 5. Mk1 / Mk2 차이 요약

| | Mk1 | Mk2 |
|---|---|---|
| 읽기 | FC01 코일 0..7 (1요청) | FC03 레지스터 1..4 (1요청) |
| 쓰기 | FC05 코일 (ch−1), 0xFF00/0x0000 | FC06 레지스터 ch, 0x0100/0x0200 |
| 채널 수 | 8 | 4 |
| 명령 이름 채널 오프셋 | `%10 − 1` | `%10` |
| uniq_id 접두 | rsw | rsw (충돌) |
