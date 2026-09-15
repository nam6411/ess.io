# 10. 신규 시스템 요구사항 (ess.io2)

## 1. 범위

### 포함
- ESP32 펌웨어 1종 (PlatformIO, Arduino 프레임워크)
- 장치 모듈 4종: **Upower, Jbdbms, RtuSwMk1, RtuSwMk2**
- 게이트웨이당 실제 장치 **1개**. 지원 모듈 중 하나와 타입별 파라미터를 선택.
- 물리 포트(UART/RS485) 정의 최대 3개 중 장치에 사용할 포트 하나를 선택.
- 웹 UI(내장, 단일 HTML) + JSON REST API: WiFi/MQTT/포트/장치 설정, 상태 조회, 스위치 제어, 재부팅, 초기화
- MQTT 발행/구독 + HA Discovery
- 설정 영속화(NVS 또는 LittleFS JSON)
- 물리 버튼 → 슬롯 스위치 토글 매핑, 상태 출력 핀 매핑
- WiFi 미설정/접속 실패 시 AP + 캡티브 포털

### 제외 (1차)
- Antbms, DS1603DA, XYMD02 (구조만 확장 가능하게)
- OTA 업데이트 (→ 미결 #Q-10, 구조상 훅만 준비)
- TLS MQTT, HTTPS
- ESP8266

## 2. 기능 요구사항

### F-SYS 시스템
| ID | 요구사항 |
|---|---|
| F-SYS-1 | 부팅 후 5초 이내에 웹 서버 응답 (WiFi 연결 여부와 무관, AP 모드 포함) |
| F-SYS-2 | 설정 미존재 시 기본 설정으로 부팅하고 AP 모드(`essio-<device_id>`, 비밀번호 설정 가능, 기본 `12341234`) 진입 |
| F-SYS-3 | STA 접속 실패가 60초 지속되면 AP를 함께 기동(AP+STA), STA 복구 시 AP 유지 여부 설정 |
| F-SYS-4 | mDNS `essio-<device_id>.local` (호스트명 설정 가능) |
| F-SYS-5 | `restart`, `factory_reset`(설정 삭제 후 재부팅) 을 웹/MQTT에서 수행 |
| F-SYS-6 | 시스템 정보(IP, RSSI, 업타임, 힙, 펌웨어 버전, 슬롯별 마지막 성공/오류 카운트)를 웹과 MQTT `<base>/sys/info`로 제공 |
| F-SYS-7 | 시리얼 로그 레벨 설정(ERROR/WARN/INFO/DEBUG), 웹에서 최근 로그 N줄 조회(링버퍼) |
| F-SYS-8 | 워치독: 메인 루프 정지 시 자동 재부팅 (Task WDT) |

### F-CFG 설정
| ID | 요구사항 |
|---|---|
| F-CFG-1 | 설정은 단일 JSON 문서로 표현되며 스키마 버전을 가짐 (`12-config-schema.md`) |
| F-CFG-2 | 웹 UI에서 저장 시 검증 → 저장 → 변경 범위에 따라 즉시 적용(MQTT 재접속, 슬롯 재초기화) 또는 재부팅 요구를 응답으로 알림 |
| F-CFG-3 | 설정 내보내기/가져오기(JSON 다운로드/업로드, 비밀번호 포함 여부 선택) |
| F-CFG-4 | 비밀번호류(WiFi, MQTT, 웹 인증)는 조회 API에서 마스킹(`"********"`), 마스킹 값으로 저장 요청 시 기존 값 유지 |

### F-NET 네트워크/MQTT
| ID | 요구사항 |
|---|---|
| F-NET-1 | WiFi STA: SSID/PW, 고정 IP(선택), 호스트명 |
| F-NET-2 | MQTT: 호스트, 포트, 사용자/비밀번호(선택), base_topic, client_id 접미, keepalive, discovery 접두(기본 `homeassistant`), discovery on/off, 발행 주기 |
| F-NET-3 | 연결 상태 머신: 비블로킹, 지수 백오프, LWT `online/offline` |
| F-NET-4 | MQTT 접속 성공 시 Discovery 전체 발행; `homeassistant/status` = `online` 수신 시 재발행 |
| F-NET-5 | 발행 실패/미접속 시 재부팅하지 않고 오류 카운트만 증가 |

### F-SLOT 슬롯/모듈
| ID | 요구사항 |
|---|---|
| F-SLOT-1 | 단일 슬롯: `enabled`, `type`(upower/jbdbms/rtusw_mk1/rtusw_mk2), `name`(표시명/slug), `port`, `slave_id`, `poll_interval_ms`, 타입별 파라미터 |
| F-SLOT-2 | 선택한 장치 하나를 설정 주기로 폴링하고 포트 뮤텍스로 명령과 직렬화 |
| F-SLOT-3 | 모듈은 `poll()` 1회당 최대 시간 예산(기본 1500ms)을 넘지 않도록 요청을 분할 가능(상태 머신) |
| F-SLOT-4 | 연속 오류 N회(기본 3) → 슬롯 `availability = offline`, 성공 시 `online` |
| F-SLOT-5 | 스위치 명령(MQTT/웹/버튼)은 폴링과 별도 큐로 처리, 쓰기 성공 시 상태 캐시 즉시 갱신 및 발행, 다음 폴링에서 검증 |
| F-SLOT-6 | 슬롯 설정 변경 시 재부팅 없이 해당 슬롯만 재생성 |

### F-UP Upower
| ID | 요구사항 |
|---|---|
| F-UP-1 | `03-device-upower.md` §3 레지스터 4블록 읽기, 스케일링, 32비트 합산 정정(U-04), 충전 상태 비트 정정(U-03) |
| F-UP-2 | 코일 4개 읽기/쓰기, 쓰기 재시도 횟수 설정(기본 3) |
| F-UP-3 | FC02 `0x2100` 실제 바이패스 상태로 비활성 출력값 마스크(기본 off) |
| F-UP-4 | 배터리 SOC/state, PV 충전 상태를 센서로 노출 |
| F-UP-5 | 파라미터: slave_id(기본 10), 레지스터 블록 on/off(불필요 블록 생략으로 폴링 단축) |

### F-JBD Jbdbms
| ID | 요구사항 |
|---|---|
| F-JBD-1 | CMD 0x03/0x04 읽기, LEN 기반 수신, 체크섬 검증 |
| F-JBD-2 | 셀 수·NTC 수를 응답에서 읽어 동적 처리(설정 `cell_count`는 Discovery 등록 수 결정, 응답과 불일치 시 경고) |
| F-JBD-3 | MOSFET 제어(CMD 0xE1), 다른 쪽 상태 보존 |
| F-JBD-4 | 보호 상태 비트를 binary_sensor 또는 정수 센서로 노출, 밸런스 비트 정수 센서 |
| F-JBD-5 | (옵션) 충전 제한: `soc_high`(기본 80) 이상이면 충전 FET OFF, `soc_low`(기본 70) 이하이면 ON. 기존 sleep_mode 대체 (#Q-6) |
| F-JBD-6 | 온도 float, cycles 정수 |

### F-RTU RtuSwMk1 / Mk2
| ID | 요구사항 |
|---|---|
| F-RTU-1 | Mk1: FC01(0,N) 읽기, FC05 쓰기. Mk2: FC03(1,N) 읽기, FC06 쓰기(0x0100/0x0200) |
| F-RTU-2 | 파라미터: slave_id(Mk1 기본 255, Mk2 기본 1), `channels`(1..8 / 1..4), 채널별 `name`, `enabled`(미사용 채널 숨김) |
| F-RTU-3 | 쓰기 후 상태 캐시 갱신, 재시도 설정 |
| F-RTU-4 | (옵션 Mk2) 모멘터리/토글 명령 (#Q-7) |

### F-IO 버튼/출력
| ID | 요구사항 |
|---|---|
| F-IO-1 | 버튼 최대 2개(확장 가능): `pin`, `active_low`, `debounce_ms`(기본 50), `action` = {slot, switch_name, mode: toggle/on/off}, `long_press_ms`(선택, 별도 action) |
| F-IO-2 | 출력 핀 최대 2개: `pin`, `active_high`, `source` = {slot, switch_name} → 상태 반영 |
| F-IO-3 | 단일 장치 기본 설정에서는 버튼·출력 매핑 없음. 필요 시 슬롯 0의 스위치에만 매핑 |

### F-WEB 웹 UI/API
`13-web-api.md` 참조.
| ID | 요구사항 |
|---|---|
| F-WEB-1 | 단일 페이지 UI(내장 gzip HTML/JS, 외부 CDN 의존 없음 — AP 모드에서도 동작) |
| F-WEB-2 | 단일 화면: 상태, 장치 종류·센서 노출, 네트워크, MQTT, 시리얼 포트 설정 |
| F-WEB-3 | 장치 최신 값·스위치 토글·온라인 상태·오류를 3초마다 갱신 |
| F-WEB-4 | 선택적 Basic Auth (사용자/비밀번호 설정 시 활성) |

## 3. 비기능 요구사항

| ID | 요구사항 |
|---|---|
| N-1 | 메인 루프 1회 ≤ 50ms (Modbus 응답 대기 제외); Modbus 응답 타임아웃 기본 500ms(설정 가능) |
| N-2 | 힙 여유 ≥ 60KB 상시 |
| N-3 | 설정 JSON ≤ 8KB |
| N-4 | 코드 구조: `src/core`(net, mqtt, config, scheduler, web), `src/modules/<type>`, `src/io`, `data/`(웹 자산) |
| N-5 | 단위 테스트 가능 부분(프레임 파서, 체크섬, 스케일링)은 순수 C++ 함수로 분리, `native` 환경 테스트 |
| N-6 | 펌웨어 버전은 빌드 시 주입(`-DFW_VERSION`) |

## 4. 미결 사항 (결정 필요)

| # | 질문 | 기본안 |
|---|---|---|
| Q-1 | 설정 저장소 | **확정 (2026-09-14)**: LittleFS `/config.json` (임시파일→rename 저장), Discovery 토픽 목록은 LittleFS `/ha-discovery.json` |
| Q-2 | 웹 프레임워크 | **확정 (2026-09-14)**: `ESP32Async/ESPAsyncWebServer` + `ESP32Async/AsyncTCP`. 핸들러는 읽기(스냅샷)만 직접 수행, 쓰기(스위치/설정 적용/재부팅)는 큐로 넘겨 메인 루프에서 처리 |
| Q-5 | 레거시 토픽 호환 모드 및 레거시 config 삭제 버튼 | 삭제 버튼만 1차, 호환 모드 2차 |
| Q-6 | Jbdbms 충전 제한 기능 정의 | F-JBD-5 안 |
| Q-7 | Mk2 모멘터리/토글 명령 | 2차 |
| Q-8 | MQTT TLS | 제외 |
| Q-9 | WebSocket 실시간 푸시 | 제외, HTTP 폴링 |
| Q-10 | OTA (ArduinoOTA / HTTP 업로드) | HTTP 업로드 2차 |
| Q-11 | 포트 3개 중 SoftwareSerial 필요 여부(UART1/2만으로 충분한지) | 하드웨어 UART 2개 + SoftwareSerial 1개 지원, 115200은 하드웨어 UART 강제 |
