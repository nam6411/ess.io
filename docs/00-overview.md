# 00. 개요 및 문서 목차

## 1. 프로젝트 목적

`ess.io2`는 기존 `ess.io` 펌웨어(`/home/nam6411/work/ess.io`, master `d631413` 기준)를
**완전히 새로 구현**하기 위한 프로젝트다. 기존 펌웨어는 다음 역할을 한다.

1. RS485(Modbus RTU) 및 UART(BMS 전용 프로토콜)로 ESS 주변장치를 주기적으로 폴링
2. 읽은 값을 MQTT로 발행하고, Home Assistant MQTT Discovery 규격으로 엔티티를 자동 등록
3. Home Assistant에서 오는 스위치 명령(`.../set`)을 구독하여 장치에 쓰기
4. 물리 버튼 2개(인버터 / 무버)로 특정 스위치를 로컬에서 토글, 상태 LED 출력
5. 웹 UI(포트 80)에서 WiFi/MQTT 접속 정보를 입력하고 EEPROM에 저장

기존 코드의 문제는 **모든 장치·핀·슬레이브ID·채널 이름이 `main.cpp`에 하드코딩**되어 있어,
장치 구성이 바뀔 때마다 펌웨어를 다시 빌드해야 한다는 점이다.

## 2. 신규 시스템 목표 (요약)

| 항목 | 기존 (ess.io) | 신규 (ess.io2) |
|---|---|---|
| 타겟 | ESP8266 + ESP32 겸용 | **ESP32 전용** |
| 장치 구성 | 코드에 고정 (Upower 1, Jbdbms 1, RtuSwMk1 1) | **웹 UI에서 슬롯 4개에 모듈 타입/파라미터 선택** |
| 지원 모듈 | Upower, Jbdbms, RtuSwMk1, RtuSwMk2, (Antbms, DS1603DA, XYMD02 – 미사용) | **Upower, Jbdbms, RtuSwMk1, RtuSwMk2** (4종) |
| 설정 저장 | EEPROM 고정 오프셋 + 자체 체크섬 | NVS(Preferences) 또는 LittleFS JSON (→ `12-config-schema.md`) |
| 웹 UI | `/settings` 단일 폼(WiFi/MQTT만) | WiFi/MQTT + 슬롯별 모듈 설정 + 상태 확인 + 제어 |
| MQTT | 하드코딩 토픽, 2가지 등록 방식 혼재 | 단일 규약 (→ `07-mqtt-homeassistant.md`) |

상세 요구사항은 `10-requirements.md`, 설계는 `11-architecture.md` 참조.

## 3. 문서 목차

### A. 기존 시스템 스펙 (역공학 결과 — "무엇을 하고 있었나")

| 문서 | 내용 |
|---|---|
| [01-legacy-system.md](01-legacy-system.md) | 기존 `main.cpp` 전체 동작: 부팅 순서, 메인 루프, 폴링 스케줄, 버튼/LED, WiFi/AP 폴백, 웹 서버, EEPROM 레이아웃, MQTT 라우팅 |
| [02-hardware.md](02-hardware.md) | 핀맵, 시리얼 포트 3개, RS485 방향 제어, 버튼/LED 전기 사양 |
| [03-device-upower.md](03-device-upower.md) | UPOWER 인버터/충전기: Modbus 입력 레지스터 맵(4블록), 코일 맵, 스케일링, 파생값, MQTT 엔티티 전체 목록 |
| [04-device-jbdbms.md](04-device-jbdbms.md) | JBD 스마트 BMS: UART 프레임 포맷, 요청/응답 명령, 바이트 오프셋별 파싱, 체크섬, MOSFET 제어, MQTT 엔티티 |
| [05-device-rtusw-mk1.md](05-device-rtusw-mk1.md) | RTU 릴레이 보드 Mk1: 코일 기반 8채널 읽기/쓰기, 토픽/채널 네이밍 규칙 |
| [06-device-rtusw-mk2.md](06-device-rtusw-mk2.md) | RTU 릴레이 보드 Mk2: 홀딩 레지스터 기반 4채널 읽기/쓰기 |
| [07-mqtt-homeassistant.md](07-mqtt-homeassistant.md) | 공통 MQTT/HA Discovery 규약: 클라이언트ID, device 블록, config/state/set 토픽, 페이로드, 구독 목록, 시스템 명령 |
| [08-legacy-issues.md](08-legacy-issues.md) | 기존 코드에서 발견된 버그·미완성·불일치 전체 목록과 신규 구현에서의 처리 방침 |
| [09-unused-drivers.md](09-unused-drivers.md) | 범위 밖 드라이버(Antbms, DS1603DA, XYMD02) 요약 — 향후 확장 시 참고 |

### B. 신규 시스템 설계 ("무엇을 만들 것인가")

| 문서 | 내용 |
|---|---|
| [10-requirements.md](10-requirements.md) | 기능/비기능 요구사항, 범위, 제약, 미결 사항 |
| [11-architecture.md](11-architecture.md) | 모듈 구조, 장치 인터페이스, 슬롯/포트 모델, 스케줄러, 상태 머신, 오류 처리 |
| [12-config-schema.md](12-config-schema.md) | 설정 데이터 모델(JSON), 저장소, 기본값, 검증 규칙, 마이그레이션 |
| [13-web-api.md](13-web-api.md) | HTTP 엔드포인트, 요청/응답 JSON, UI 화면 구성, 인증 |

### C. 사용/운영

| 문서 | 내용 |
|---|---|
| [14-user-manual.md](14-user-manual.md) | 도구 설치, 빌드/업로드, 최초 설정(AP), 운영 API, HA 연동, 문제 해결, 개발자 안내 |

## 4. 용어

| 용어 | 의미 |
|---|---|
| UPOWER | EPEVER UPower 계열 하이브리드 인버터/충전 컨트롤러 (Modbus RTU, 기존 코드의 `Upower` 클래스, 토픽 도메인 `upower`, 과거 도메인 `epever`) |
| JBD BMS | JBD(Jiabaida) 스마트 BMS, UART 0xDD 프로토콜 (기존 `Jbdbms` 클래스) |
| RTU SW Mk1 / Mk2 | Modbus RTU 릴레이 보드 2세대. Mk1은 코일(FC01/05), Mk2는 홀딩 레지스터(FC03/06) 사용 |
| 슬롯(slot) | 신규 설계에서 장치 모듈 인스턴스가 장착되는 논리 위치 (0~3, 최대 4개) |
| 포트(port) | 물리 UART/RS485 채널 (ESP32 UART1/UART2 + 필요 시 SoftwareSerial) |
| HA | Home Assistant |
| Discovery | HA MQTT Discovery — `homeassistant/<component>/<...>/config` 토픽에 JSON을 retain 발행하여 엔티티를 자동 생성하는 규약 |
| device_id | 칩 고유 ID(MAC 기반 6자리 HEX). 클라이언트ID·uniq_id·HA device identifiers에 사용 |
