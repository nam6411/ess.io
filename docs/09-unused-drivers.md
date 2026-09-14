# 09. 범위 밖 드라이버 요약 (Antbms, DS1603DA, XYMD02)

기존 `ess.io`에 구현되어 있으나 `main.cpp`에서 인스턴스화되지 않은 드라이버. 신규 1차 범위에서 제외하되,
슬롯 모듈 인터페이스가 정의되면 같은 형태로 추가 가능하도록 핵심 프로토콜을 기록한다.

## 1. Antbms (ANT BMS, UART 19200 8N1)

- 요청: `5A 5A 00 00 01 01` → 응답 **140바이트 고정**, 헤더 `AA 55 AA FF`.
- 체크섬: 바이트 4..137 합산, 하위 8비트(`checksum_l`)와 캐리 누적(`checksum_u`) → `[138]=u, [139]=l`.
- 주요 오프셋: `[4..5]` 총전압/1000 V, `[6..69]` 셀 1..32 전압 /1000 V (2바이트씩), `[70..73]` 전류 s32/10 A (부호 반전), `[74]` SOC, `[75..78]` 만충용량 /1e6 Ah, `[79..82]` 잔량 /1e6 Ah, `[83..86]` 사이클 /1000, `[87..90]` 가동 초, `[91..92]` MOSFET 온도, `[93..94]` 밸런스 온도, `[95..96]` 셀1 온도, `[97..98]` 셀5 온도, `[103]` 충전 MOSFET 상태코드, `[104]` 방전 MOSFET 상태코드, `[105]` 밸런스 상태코드.
- 상태코드 문자열 테이블: 충전 13종, 방전 15종, 밸런스 6종 (`antbms.cpp` 상단 배열).
- 파라미터 읽기: `5A 5A <addr> 00 00 <addr>` → 6바이트 응답 `[3..4]` 값, 체크섬 `[2]+[3]+[4] == [5]`.
- 파라미터 쓰기: `A5 A5 <addr> <hi> <lo> <chk=(addr+hi+lo)&0xFF>`.
- MOSFET 제어: 방전 ON `A5 A5 F9 00 01 FA`, OFF `A5 A5 F9 00 00 F9`; 충전 ON `A5 A5 FA 00 01 FB`, OFF `A5 A5 FA 00 00 FA`.
- 완속 충전: 파라미터 addr 9 = 10(완속) / 1000(급속). `check_sleep_mode`가 SOC 50% 기준으로 전환 (Antbms에서는 실제 호출됨).
- MQTT: 도메인 `antbms`, 단일 JSON `homeassistant/sensor/antbms/state`, 스위치 `charge_mosfet/discharge_mosfet/sleep_mode`.
- 기존 결함: `setup_entity` 중복 발행 블록, `%S`(와이드 문자) 포맷 사용, `balance_status_describe`가 방전 테이블을 참조, 미초기화 `strbuf` 누수.

## 2. DS1603DA (초음파 액위 센서, Modbus RTU 9600)

- 기본 슬레이브 0x01, 코드에서는 0xB1, 0xB2…로 재할당해 다중 장치 구분 (`levelerId = slaveID − 0xB0`).
- 읽기: FC03 `0x0000` 보정값(mm), `0x0001` 실시간값(mm). 코드는 `readHoldingRegisters(0, 2)` 후 `[0]`만 사용.
- 설정: FC06 `0x0004` 슬레이브 주소(0x01~0xF7), `0x0005` 매질(1 물 / 2 오일), `0x0006` 측정 주기(1~60초).
- MQTT: `homeassistant/sensor/ds1603da/gas<n>/{config,state}`, JSON `{"level":mm,"percentage":0}` (percentage 계산 없음).
- 헤더 주석에 프레임 예제/레지스터 표(한국어) 상세 기록 — 필요 시 `ess.io/src/ds1603da.h` 참조.

## 3. XYMD02 (온습도 센서, Modbus RTU 9600)

- 슬레이브 0xB3… (`levelerId = slaveID − 0xB3`).
- 읽기: FC04 `readInputRegisters(0x0001, 2)` → `[0]` 온도 s16/10 ℃, `[1]` 습도 s16/10 %.
- Z-score 이상치 필터: lag 5, threshold 3.5, influence 0.5. 온·습도 **둘 다** 이상 신호일 때만 발행 억제(`zscore_status = 0`), 아니면 발행. 초기 5샘플은 워밍업.
  (표준편차 계산에 `sqrt(Σ)/N` 사용 — 수학적으로 표준편차가 아님. 재사용 시 `sqrt(Σ/N)`으로 정정.)
- MQTT: `homeassistant/sensor/xymd02/humidity<n>/state` JSON `{"temperature","humidity"}`, config는 `humidity<n>`, `temperature<n>` 두 개. 추가로 `humidity0/temperature0` 하드코딩 발행(중복).

## 4. 신규 시스템에서의 위치
`11-architecture.md`의 `IDeviceModule` 인터페이스를 구현하고 `ModuleRegistry`에 타입을 등록하면 슬롯 선택 목록에 나타나도록 설계한다. 1차 릴리스에서는 등록하지 않는다.
