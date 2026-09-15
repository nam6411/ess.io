# 08. 기존 코드 결함 목록 및 신규 처리 방침

분류: **B** 버그(동작 오류), **C** 컴파일 불가, **D** 죽은 코드/미완성, **S** 보안/안정성, **I** 불일치/설계 부채.
"신규 방침"은 ess.io2에서 어떻게 할지.

## 공통 / main.cpp

| # | 분류 | 위치 | 내용 | 신규 방침 |
|---|---|---|---|---|
| L-01 | S | `messageRouter` | 수신 페이로드를 `char[20]`에 길이 검사 없이 복사 → 오버플로 | 길이 제한 + 널 종료 |
| L-02 | S | `handleSetSSID` | 폼 값 6개를 `char[30]`에 `strcpy` → 오버플로 | 길이 검증(64자) 후 저장 |
| L-03 | S | `read_word` | 슬롯 32B인데 35B까지 스캔, 출력 버퍼 30B → 오버플로 | NVS(Preferences)/JSON 사용 |
| L-04 | B | `webRootHandler` | `/` 에 응답 없음 | `/` = SPA 메인 |
| L-05 | B | `setup_wifi` | SSID 설정됐으나 접속 실패 시 AP 미기동 → 재설정 불가 | 접속 실패 N초 후 AP+STA 동시 기동 (captive portal) |
| L-06 | B | `setup_wifi` | 연결 시도 최대 ~50초 블로킹 | 비동기 WiFi 이벤트 기반 |
| L-07 | B | `handleSetSSID` | 이미 연결 중이면 새 SSID 미적용 | 저장 후 재접속 or 재부팅 선택 |
| L-08 | S | ISR | `Serial.printf` 호출, 디바운스 없음 | ISR은 타임스탬프/플래그만, 루프에서 디바운스 |
| L-09 | I | 버튼 | 임계값 5 = 채터링 엣지 수에 의존 | 디바운스 30ms + 짧게/길게 누름 구분 설정 |
| L-10 | D | `detectMultiplePress`, `delete_params`, `getWifiInfo`, `enum RS485TYPE`, `preTransmission/postTransmission`, `lastMsg2`, `lastMoverButon` 등 | 미호출/미사용 | 제거 |
| L-11 | B | `resetFunc` (주소 0 호출) | ESP32에서 정의되지 않은 동작(패닉 후 재부팅) | `ESP.restart()` |
| L-12 | B | `Device::mqtt_publish` | 미접속 시 즉시 `ESP.restart()` — WiFi 순단마다 재부팅 | 발행 실패는 카운트만, 재접속 로직에 위임 |
| L-13 | B | `Device::mqtt_publish` | 매 발행 `delay(100)` → Upower 1사이클 ≥0.9s 블로킹 | 지연 제거, `mqtt.loop()` 정기 호출 |
| L-14 | B | `Device::subscribe` | 같은 토픽 2회 구독 | 1회 |
| L-15 | I | `setup_entity` | 부팅 시 Discovery 미발행(`/reset` 필요) | 부팅·MQTT 접속 시 자동 발행 + HA `birth` 메시지(`homeassistant/status` = online) 수신 시 재발행 |
| L-16 | I | 폴링 | 2초마다 장치 1개 라운드로빈, 동기 블로킹 | 슬롯별 독립 주기, 상태 머신, 포트 뮤텍스 |
| L-17 | I | `comm_info` | rx/tx/baud 값이 무시됨(포트는 main이 고정) | 포트 설정 → 슬롯 바인딩 |
| L-18 | I | `device_id` | 전역 + 각 Device 인스턴스에서 중복 계산; `char device_id[300]` | 단일 유틸 |
| L-19 | S | 웹 | 인증 없음, 비밀번호 평문 표시 | 선택적 Basic Auth, 비밀번호 필드 마스킹, 저장된 비밀번호는 응답에 미포함 |
| L-20 | D | WebSocket 81 | 스텁 | 제거 또는 실시간 상태 푸시(선택, → 미결 #Q-9) |
| L-21 | I | `Device` | Discovery 생성기 3종(`assemble_*` char/String, `mqtt_register`) 혼재 | 단일 빌더 |
| L-22 | I | HA device 블록 | model이 `"ESP8266"`/`"ESP32"` 혼재, name 고정 "Battery" | 설정 가능한 device_name |
| L-23 | B | `setup_mqtt` | `char mqtt_client_name[20]`, 최대 12자라 안전하나 여유 없음 | String |

## Upower

| # | 분류 | 위치 | 내용 | 신규 방침 |
|---|---|---|---|---|
| U-01 | **C** | `update_data()` 285행 | `bypass.voltage = 0.0` 세미콜론 누락 | — |
| U-02 | **C** | `update_data()` 287행 | 존재하지 않는 멤버 `inverter.*` (있는 것은 `inverter_in`/`inverter_out`) | 의도는 `inverter_out` |
| U-03 | B | `pv_charge.state` | 충전 단계는 D3–D2 | `(reg >> 2) & 0x03` |
| U-04 | B | `bypass.wattage` | `.../100; + high*65536/100` 세미콜론으로 상위 워드 무시 | 32비트 합산 |
| U-05 | B | `change_switch` | 알 수 없는 이름 → `switch_state[-1]` 쓰기 | 이름 검증 |
| U-06 | B | `getStringSwitchEnum` | 모든 분기 실패 시 반환값 없음(UB) | 제거 |
| U-07 | I | Discovery | V/A/W/Hz에 device_class `energy` | 올바른 class + state_class |
| U-08 | I | Discovery | uniq `oybw` 오타, `bypass_out_*` 이름과 "Bypass In" 표시명 불일치 | 재정의 |
| U-09 | D | Discovery | `battery.soc`, `battery.state`, `pv.state`, `inverter.temperature`(항상 0) | soc/state/pv.state 노출, inverter temp 제거 |
| U-11 | I | `mqtt_publish("…/config","")` 40여 회 | 부팅마다 레거시 삭제 발행 | 1회성 "레거시 정리" 버튼 |

## Jbdbms

| # | 분류 | 위치 | 내용 | 신규 방침 |
|---|---|---|---|---|
| J-01 | B | `parseBasicData` | `Serial.printf("…%d\n" + number_of_temp_sensor)` 포인터 산술 → 쓰레기 출력 | — |
| J-02 | B | NTC 온도 | 정수 나눗셈 → 1℃ 해상도 | float |
| J-03 | B | `asking` | 고정 50바이트 수신 | LEN 기반 수신 |
| J-04 | I | Discovery | `Discharge Wattage`, `Cycle` → device_class energy/Ah; `Cell Resist` voltage | power/W, 정수, 제거 또는 mΩ |
| J-05 | D | `cell_resist_N` | 등록만 되고 발행 없음 | 제거(옵션) |
| J-06 | D | `bms_temp_k` | 발행되나 등록 없음 | 등록 |
| J-07 | B | `publish_data` | `cycle`이 문자열 | 정수 |
| J-08 | S | `publish_data` | `sprintf` 이어붙이기, 1000B 버퍼 무검사 (16셀+NTC 4개 ≈ 500B라 현재는 안전) | ArduinoJson |
| J-09 | D | `check_sleep_mode` | 미호출, `slow_charge` 갱신 없음 | 충전 제한 기능으로 재정의(#Q-6) |
| J-10 | B | 생성자 | `cell_data[30] = {0.0,}` — 배열 범위 밖 인덱스 30에 대입 | 멤버 초기화자 |
| J-11 | I | `slave_id` | 프로토콜 주소가 아닌 토픽 번호 | 슬롯 인덱스로 대체 |
| J-12 | D | 미파싱 필드 | 보호상태/밸런스/셀수/SW버전 | 보호상태·밸런스 노출 |

## RtuSwMk1 / Mk2

| # | 분류 | 위치 | 내용 | 신규 방침 |
|---|---|---|---|---|
| R-01 | B | `change_switch` 재시도 | `while (retVal = f() == OK)` 우선순위 → 성공 시 반복 쓰기, 실패 시 탈출 | 명시적 재시도 |
| R-02 | B | 동일 | `count` 미초기화 | — |
| R-03 | B | 동일 | `isSuccessRequest = (retVal == ku8MBSuccess)`에서 retVal은 0/1 | — |
| R-04 | B | Mk1 | 쓰기 후 `switch_state[0-based]`, 읽기는 1-based | 1-based 통일 |
| R-05 | B | Mk1 생성자 호출 | `numOfSW=8`인데 role 7개 → `role[8]` 미초기화 포인터를 Discovery name으로 사용 | 설정에서 채널 수 = 이름 배열 길이 |
| R-06 | I | 슬레이브 주소 | 논리 ID(0)와 실제 송신 주소(255) 분리, `prepareModbus` 미호출 | 슬롯 설정의 `slave_id`를 실제 프레임 주소로 사용(0xFF 허용) |
| R-07 | I | 명령 이름 `"<id><ch>"` | 한 자리 제한 | `ch<n>` |
| R-08 | I | uniq_id `rsw<id><ch>` | Mk1/Mk2 충돌 | `<device_id>_<slot>_ch<n>` |
| R-09 | S | `sprintf(..., "...\0")` 문자열 리터럴 내 `\0` | 무해하나 의도 불명 | — |
| R-10 | B | `char this_uniq_id[6]` | `rsw` + 2자리 + NUL = 6 → id/ch 두 자리면 오버플로 | String |

## 신규 구현 원칙 (요약)
1. 모든 문자열 버퍼는 `String`/ArduinoJson 또는 길이 검증된 `snprintf`.
2. 블로킹 `delay` 금지(Modbus 응답 대기 제외, 타임아웃 단축).
3. ISR은 최소화.
4. 장치 오류는 카운트 후 `availability` 반영, 재부팅 금지.
5. 설정은 스키마로 검증 후 저장.
