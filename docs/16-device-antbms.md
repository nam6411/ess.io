# 16. ANT BMS

ANT BMS(중국 ANT/蚂蚁 보호보드)의 UART 프로토콜과 `antbms` 드라이버. 소스: `src/modules/antbms/`.

**출처** — [syssi/esphome-ant-bms](https://github.com/syssi/esphome-ant-bms) (`components/ant_bms`, `ant_bms_old`)의
구현과 실측 프레임. 제조사 문서 `docs/ANT_communication_protocol_EN.1.pdf`도 그 저장소에 있다.
구형 프로토콜은 기존 펌웨어 요약([09-unused-drivers.md](09-unused-drivers.md) §1)과도 대조했다.

## 1. 연결

| 항목 | 값 |
|---|---|
| 인터페이스 | UART TTL (BMS 쪽 RS485 모듈이 있으면 RS485도 가능) |
| 속도 | **19200 8N1** (두 세대 공통) |
| 주소 | 없음 (1:1). 모드 선택 시 `slave_id = 0` |
| 폴링 | 기본 5초 |

두 세대의 프로토콜이 있다. 기판 세대로 갈리며 겉모양으로 구분하기 어렵다.

| 구분 | 신형 (2021~) | 구형 |
|---|---|---|
| 예 | ANT-BMS 16S/24S 신형 앱(ANT BMS), 16ZMUB00-211026A 등 | ANT-BLE16ZMUB, ANT-BLE24BHUB 등 |
| 프레임 | `7E A1 … AA 55`, 가변 길이 | 요청 6바이트, 응답 **140바이트 고정** |
| 무결성 | Modbus CRC16 | 바이트 합 |
| 엔디언 | 리틀 | 빅 |

`params.protocol = "auto"`(기본)면 신형·구형 요청을 번갈아 보내다가 먼저 응답한 쪽으로 고정한다(로그
`antbms: new protocol detected`). 상태 JSON의 `protocol`로도 확인할 수 있다.

## 2. 신형 프로토콜

### 2.1 프레임
```
7E A1 <func> <addr lo> <addr hi> <len> <data × len> <crc lo> <crc hi> AA 55
```
- 길이 = 6 + len + 4. CRC = Modbus CRC16(초기 0xFFFF, 다항 0xA001)을 **바이트 1부터 data 끝까지** 계산.

| 요청 | 프레임 |
|---|---|
| 상태 읽기 | `7E A1 01 00 00 BE 18 55 AA 55` → 응답 func `0x11` |
| 쓰기 인증 | `7E A1 23 6A 01 0C "123456789abc" <crc> AA 55` (기본 암호) |
| 레지스터 쓰기 | `7E A1 51 <reg> 00 00 <crc> AA 55` (인증 직후) |

### 2.2 스위치 (레지스터에 0을 쓴다 — 켜기·끄기 레지스터가 따로 있다)

| 스위치 | 켜기 | 끄기 |
|---|---|---|
| 방전 MOSFET | `0x03` | `0x01` |
| 충전 MOSFET | `0x06` | `0x04` |
| (밸런서) | `0x0D` | `0x0E` | 드라이버 미노출 |
| (부저) | `0x1E` | `0x1F` | 드라이버 미노출 |

### 2.3 상태 프레임 (func 0x11)
`T` = 온도 센서 수(바이트 8), `C` = 셀 수(바이트 9), `off = 2C + 2T`.

| 오프셋 | 크기 | 내용 | 배율 |
|---|---|---|---|
| 7 | 1 | 배터리 상태 (0 Unknown, 1 Idle, 2 Charge, 3 Discharge, 4 Standby, 5 Error) | |
| 8 / 9 | 1 / 1 | 온도 센서 수 / 셀 수 | |
| 10 / 18 / 26 | 8 | 보호 / 경고 / 밸런싱 비트마스크 | |
| 34 + 2i | 2 | 셀 i 전압 | 0.001 V |
| 34 + 2C + 2i | 2 (s) | 셀 온도 i | 1 °C |
| 34 + off | 2 (s) | MOSFET 온도 | 1 °C |
| 36 + off | 2 (s) | 밸런서 온도 | 1 °C |
| 38 + off | 2 | 팩 전압 | 0.01 V |
| 40 + off | 2 (s) | 전류 (+ 충전, − 방전) | 0.1 A |
| 42 + off | 2 | SOC | 1 % |
| 44 + off | 2 | SOH | 1 % |
| 46 / 47 / 48 + off | 1 | 충전 / 방전 MOSFET, 밸런서 상태 코드 (§4) | |
| 50 + off | 4 | 설정 전체 용량 | 0.000001 Ah |
| 54 + off | 4 | 잔량 | 0.000001 Ah |
| 58 + off | 4 | 누적 사이클 용량 | 0.001 Ah |
| 62 + off | 4 (s) | 전력 | 1 W |
| 66 + off | 4 | 누적 가동 시간 | 1 s |
| 74 / 78 / 82 + off | 2 | 최고 / 최저 셀 전압, 셀 편차 | 0.001 V |

## 3. 구형 프로토콜

| 요청 | 프레임 |
|---|---|
| 상태 읽기 | `5A 5A 00 00 01 01` → 140바이트 |
| 충전 MOSFET ON / OFF | `A5 A5 FA 00 01 FB` / `A5 A5 FA 00 00 FA` |
| 방전 MOSFET ON / OFF | `A5 A5 F9 00 01 FA` / `A5 A5 F9 00 00 F9` |

명령 6바이트 = `<func> <func> <addr> <hi> <lo> <addr+hi+lo>`. 응답 헤더 `AA 55 AA FF`, 체크섬 = 바이트 4..137의 합(16비트)을
138..139에 빅 엔디언으로.

| 오프셋 | 크기 | 내용 | 배율 |
|---|---|---|---|
| 4 | 2 | 팩 전압 | **0.1 V** (docs/09의 "/1000"은 오기 — 541 → 54.1 V) |
| 6 + 2i | 2 | 셀 i 전압 (최대 32) | 0.001 V |
| 70 | 4 (s) | 전류 (+ 충전, − 방전) | 0.1 A |
| 74 | 1 | SOC | % |
| 75 / 79 / 83 | 4 | 설정 용량 / 잔량 / 누적 사이클 용량 | 0.000001 / 0.000001 / 0.001 Ah |
| 87 | 4 | 가동 시간 | s |
| 91 / 93 | 2 (s) | MOSFET / 밸런서 온도 | °C |
| 95..101 | 2 (s) | 셀 온도 4개 | °C |
| 103 / 104 / 105 | 1 | 충전 / 방전 MOSFET, 밸런서 상태 코드 | |
| 111 | 4 (s) | 전력 | W |
| 115 / 116 | 1 / 2 | 최고 전압 셀 번호 / 전압 | 0.001 V |
| 118 / 119 | 1 / 2 | 최저 전압 셀 번호 / 전압 | 0.001 V |
| 121 | 2 | 평균 셀 전압 | 0.001 V |
| 123 | 1 | 셀 수 | |

> 전류 부호: ESPHome의 실측 프레임(방전 중 −10.7 A)을 기준으로 **충전 +, 방전 −**로 그대로 쓴다.
> 기존 펌웨어는 구형 전류의 부호를 뒤집었다. 장치에서 충전이 음수로 보이면 `params.invert_current = true`.

## 4. MOSFET 상태 코드
`0` Off, `1` On, 그 밖은 차단 사유. 충전: 2 과충전, 3 과전류, 4 만충, 5 총전압 초과, 6 배터리 과열, 7 MOSFET 과열,
8 전류 이상, 9 밸런스선 이탈, 10 기판 과열, 12 개방 실패, 13 방전 MOSFET 이상, 14 대기, 15 수동 끔, 16 2단 과전압,
17 저온, 18 셀 편차 초과, 20 자가 진단 오류. 방전: 2 과방전, 3 과전류, 4 2단 과전류, 5 총전압 부족, 6~10 동일,
11 충전 MOSFET 켜짐, 12 단락, 13 방전 MOSFET 이상, 14 개방 실패, 15 수동 끔, 16 2단 저전압, 17 저온, 18 셀 편차 초과,
19 자가 진단 오류. 상태 JSON에는 문자열(`chg_status`, `dis_status`, `bal_status`)로 들어간다.

## 5. 드라이버

| 항목 | 내용 |
|---|---|
| type / 모드 | `antbms` — "ANT BMS", 슬롯 label 기본 `BMS`, 토픽 `rv/bms` |
| params | `protocol` (`auto`·`new`·`old`), `invert_current` (false), `expose_cells` (true) |
| 상태 JSON | JBD와 같은 키: `pack_v`, `current`, `power`(= 전압×전류, + 충전), `soc`, `remain_ah`, `full_ah`, `cell_diff`, `chg_fet`, `dis_fet`, `cell_v[]` + ANT 전용 `soh`, `cycle_ah`, `cell_max`, `cell_min`, `mos_temp`, `bal_temp`, `temp[]`, `*_status`, `runtime_s`, `protocol` |
| 스위치 | `charge_fet`, `discharge_fet`. 신형은 인증 후 레지스터 쓰기, 구형은 A5 명령. ACK는 보지 않고 1초 뒤 상태로 확인 |
| 디스플레이 | `keySensors = soc,power,pack_v,current,cell_diff`, `ring = battery:soc,power` (JBD와 같음) |
| 테스트 | `pio test -e native` — ESPHome 실측 프레임(신형 16S, 구형 8S)으로 해석 결과 검증 |

**실기 미검증** — 두 프로토콜 모두 실제 ANT BMS에 붙여 본 적은 없다. 처음 붙일 때 로그의
`antbms: … protocol detected`와 상태 JSON 값(특히 전류 부호)을 확인할 것.
