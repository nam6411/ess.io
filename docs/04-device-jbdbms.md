# 04. 장치 모듈: JBD 스마트 BMS (Jbdbms)

> 소스: `ess.io/src/jbdbms.h`, `jbdbms.cpp`. 도메인 이름 `jbdbms_<slave_id>` (기존 인스턴스: `jbdbms_0`).

## 1. 통신

| 항목 | 값 |
|---|---|
| 물리 | UART TTL (BMS의 UART 포트), 9600 8N1 |
| 기존 포트 | SoftwareSerial RX16 / TX17 |
| 프로토콜 | JBD(Jiabaida) 0xDD 프레임 |
| 주소 | 없음 (1:1). 생성자 `slave_id`는 토픽 번호로만 사용 |
| 생성자 인자 | `cell_series` = 직렬 셀 수 (기존 16) |

## 2. 프레임 포맷

### 2.1 요청 (호스트 → BMS)
```
DD  A5  <CMD>  <LEN=00>  <CHK_H>  <CHK_L>  77          (읽기, 7바이트)
DD  5A  <CMD>  <LEN>     <DATA...> <CHK_H> <CHK_L> 77  (쓰기)
```
- `0xA5` = 읽기, `0x5A` = 쓰기.
- 체크섬: `CHK = 0x10000 - Σ(CMD, LEN, DATA...)` 의 하위 16비트, 빅엔디안.

### 2.2 응답 (BMS → 호스트)
```
DD  <CMD>  <STATUS>  <LEN>  <DATA[LEN]>  <CHK_H>  <CHK_L>  77
```
- `STATUS` 0x00 = OK, 0x80 = 오류.
- 총 길이 = LEN + 7.
- 체크섬: `0x10000 - Σ(STATUS, LEN, DATA...)` (프레임 인덱스 2 ~ size-4).

### 2.3 기존 검증 규칙 (`checksumCalc`)
1. `frame[0] == 0xDD && frame[size-1] == 0x77`
2. `frame[2] == 0x00` (STATUS OK)
3. `frame[3] == size - 7`
4. 계산 체크섬 == `frame[size-3..size-2]`
실패 시 -1.

### 2.4 송수신 절차 (`asking(req, req_len, min_len)`)
1. 수신 버퍼 비우기
2. `serial.write(req, req_len)`
3. `delay(100)`
4. `serial.readBytes(buf, 50)` — 최대 50바이트, Stream 타임아웃(기본 1000ms)까지 대기
   - **16셀 전압 응답은 16×2+7 = 39바이트, 기본 응답 34바이트 이상이므로 50으로 충분하지만 셀 수·NTC 수가 늘면 잘림**. 신규: `LEN` 필드를 읽고 필요한 만큼 수신.
5. `size < min_len` → -3, 체크섬 실패 → -4, 성공 시 size 반환

## 3. 명령

### 3.1 기본 정보 읽기 CMD 0x03
요청: `DD A5 03 00 FF FD 77`. 최소 응답 길이 34.

응답 DATA 오프셋 (프레임 인덱스 = DATA 인덱스 + 4):

| DATA idx | 프레임 idx | 크기 | 필드 | 변환 | 기존 코드 |
|---|---|---|---|---|---|
| 0–1 | 4–5 | u16 | 총 전압 | /100 V | `battery_all_voltage` |
| 2–3 | 6–7 | **s16** | 전류 (+충전 / −방전) | /100 A | `battery_current` |
| 4–5 | 8–9 | u16 | 잔여 용량 | /100 Ah | `battery_capacity` |
| 6–7 | 10–11 | u16 | 정격(만충) 용량 | /100 Ah | `battery_full_capacity` (읽기만, 미발행) |
| 8–9 | 12–13 | u16 | 사이클 횟수 | 정수 | `battery_cycle` (float로 저장) |
| 10–11 | 14–15 | u16 | 생산일자 (bit: y7 m4 d5) | | 미사용 |
| 12–15 | 16–19 | u32 | 밸런스 상태 비트 (셀 1–32) | | 미사용 |
| 16–17 | 20–21 | u16 | 보호 상태 비트 | | 미사용 |
| 18 | 22 | u8 | SW 버전 | | 미사용 |
| 19 | 23 | u8 | RSOC (%) | 정수 | `bms_soc` |
| 20 | 24 | u8 | FET 상태 | bit0 = 충전 MOSFET ON, bit1 = 방전 MOSFET ON | `charge_mosfet_status`, `discharge_mosfet_status` |
| 21 | 25 | u8 | 셀 수 | | 미사용 (cell_series 하드코딩) |
| 22 | 26 | u8 | NTC 개수 N | | `number_of_temp_sensor` |
| 23+2k | 27+2k | u16 | NTC k 온도 (0.1K) | `(raw − 2731) / 10` ℃ | `temp[k]` (**정수 나눗셈 → 1℃ 해상도**) |

- 보호 상태 비트(DATA 16–17) 의미(JBD 규격): bit0 셀 과충전, bit1 셀 과방전, bit2 팩 과충전, bit3 팩 과방전, bit4 충전 과열, bit5 충전 저온, bit6 방전 과열, bit7 방전 저온, bit8 충전 과전류, bit9 방전 과전류, bit10 단락, bit11 IC 오류, bit12 MOSFET 소프트웨어 락. **신규에서 센서로 노출 권장**(→ 요구사항 F-JBD-4).
- 신규는 DATA[21] 셀 수를 읽어 `cell_series` 설정값과 불일치 시 경고.

### 3.2 셀 전압 읽기 CMD 0x04
요청: `DD A5 04 00 FF FC 77`. 최소 응답 길이 `cell_series*2 + 7`.

| DATA idx | 프레임 idx | 필드 | 변환 |
|---|---|---|---|
| 2(i−1), 2(i−1)+1 | 2+2i, 3+2i (i=1..N) | 셀 i 전압 | /1000 V |

- `LEN` = 셀 수 × 2.
- 파생: `cell_diff = max − min` (V). 셀 내부저항 추정 `cell_resi[i] = (V_prev − V_now) / I × 1000` (mΩ, 조악한 추정, 미발행).

### 3.3 MOSFET 제어 CMD 0xE1 (쓰기)
요청 9바이트:
```
DD 5A E1 02 00 <VAL> FF <0x1D − VAL> 77
```
- `VAL` bit0 = 1 → **충전 MOSFET 차단**, bit1 = 1 → **방전 MOSFET 차단** (0 = 허용).
- 체크섬 `0x10000 − (0xE1 + 0x02 + 0x00 + VAL) = 0xFF1D − VAL`.
- 기존 코드:
  - `set_charge_mosfet(on)`: `VAL = (!discharge_status)*2 + (!on)`
  - `set_discharge_mosfet(on)`: `VAL = (!on)*2 + (!charge_status)`
  → 다른 쪽 MOSFET의 현재 캐시 상태를 유지하면서 한쪽만 변경.
- 응답 최소 길이 5. 응답 후 캐시 상태는 갱신하지 않음(다음 폴링에서 반영).

### 3.4 (미구현) 기타 JBD 명령
- `0x05` 하드웨어 버전 문자열, `0xE0`~ 파라미터 읽기/쓰기(팩토리 모드 진입 필요). 범위 외.

## 4. 폴링 (`update_data`)
1. CMD 0x03 → 성공 시 `parseBasicData`
2. CMD 0x04 → 성공 시 `parseVoltageData`
3. 반환 0 (오류 무시). `update_switch()`는 아무것도 안 함(MOSFET 상태는 0x03에 포함).

## 5. 슬립 모드 / 완속 충전 로직 (`check_sleep_mode`, **기존에서는 호출되지 않음**)

`sleep_mode` 스위치(가상, BMS와 무관, MQTT로만 토글) ON일 때:
- SOC > 50% 이고 충전 MOSFET ON → 충전 MOSFET OFF (완속=차단)
- SOC < 50% 이고 `slow_charge`(항상 false — 갱신 코드 없음) → 충전 MOSFET ON
OFF일 때: 충전 MOSFET이 ON이면 다시 ON (무의미)
→ 로직이 미완성. 신규에서는 "SOC 상한/하한 충전 제한" 기능으로 재정의 (→ 요구사항 F-JBD-5, 미결 #Q-6).

## 6. MQTT 인터페이스 (기존 — `Device::mqtt_register` 신규 방식 사용)

`device_name = "jbdbms"`, `device_no = slave_id` (0). 접두 `P = homeassistant/<sensor|switch>/jbdbms_0`.

### 6.1 구독
```
homeassistant/switch/jbdbms_0/charge_mosfet/set
homeassistant/switch/jbdbms_0/discharge_mosfet/set
homeassistant/switch/jbdbms_0/sleep_mode/set
```

### 6.2 스위치 상태 발행
```
homeassistant/switch/jbdbms_0/charge_mosfet/state     ON|OFF
homeassistant/switch/jbdbms_0/discharge_mosfet/state  ON|OFF
homeassistant/switch/jbdbms_0/sleep_mode/state        ON|OFF
```

### 6.3 측정값 발행 — 단일 JSON
토픽 `homeassistant/sensor/jbdbms_0/jbdbms/state`
```json
{"discharge_voltage":53.20,"discharge_current":-3.50,"discharge_wattage":-186.20,
 "capacity":95.00,"cycle":"12.000000",
 "cell_1":3.325, ... ,"cell_16":3.330,
 "bms_temp_0":25.000,"bms_temp_1":26.000,
 "soc":92,"cell_diff":0.012}
```
- `cycle`은 문자열(`"%f"`) — HA에서 숫자 변환 필요. 신규: 정수.
- `cell_resist_N`은 Discovery 등록만 되고 페이로드에 없음(항상 unavailable).
- `bms_temp_k`는 페이로드에 있으나 Discovery 미등록.
- `sprintf` 이어붙이기, 버퍼 1000바이트, 길이 검사 없음.

### 6.4 HA Discovery 엔티티 (`mqtt_register` 호출 순서)

| name | entity(=value_json 키) | device_class | unit | 종류 |
|---|---|---|---|---|
| Discharge Voltage | discharge_voltage | voltage | V | sensor |
| Discharge Current | discharge_current | current | A | sensor |
| Discharge Wattage | discharge_wattage | energy | Ah (**오류: W여야 함**) | sensor |
| Capacity | capacity | energy | Ah | sensor |
| SoC | soc | battery | % | sensor |
| Cycle | cycle | energy | Ah (**오류**) | sensor |
| Cell Diff | cell_diff | voltage | V | sensor |
| Charge Mosfet | charge_mosfet | switch | | switch |
| Discharge Mosfet | discharge_mosfet | switch | | switch |
| Sleep Mode | sleep_mode | switch | | switch |
| Cell 1..N | cell_1..cell_N | voltage | V | sensor |
| Cell Resist 1..N | cell_resist_1..N | voltage (**오류**) | V | sensor |

- config 토픽: `homeassistant/sensor/jbdbms_0/<entity>/config`, `homeassistant/switch/jbdbms_0/<entity>/config`
- uniq_id: `<entity>_0_<device_id>`
- 발행: 빈 페이로드(retain) → JSON(retain).

## 7. 신규 구현 시 데이터 모델 제안

```
JbdbmsState {
  float pack_voltage, current, remaining_ah, full_ah;  uint16 cycles;
  uint8 soc;  bool chg_fet, dis_fet;
  uint16 protection_bits;  uint32 balance_bits;
  uint8 cell_count, ntc_count;
  float cell_v[32];  float ntc_c[8];  float cell_diff;
  uint32 last_ok_ms;  uint8 consecutive_errors;
}
```
