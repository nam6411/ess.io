# 13. 웹 서버 / REST API / UI 명세

포트 80. 모든 API는 `application/json`. 인증 활성 시 HTTP Basic Auth (`web.auth`).
오류 응답: `{"error": "<code>", "message": "<사람용 메시지>", "field": "<선택>"}`.

## 1. 정적
| 메서드 | 경로 | 설명 |
|---|---|---|
| GET | `/` | `index.html.gz` (Content-Encoding: gzip), 캐시 `max-age=86400`, 펌웨어 버전으로 ETag |
| GET | `/generate_204`, `/hotspot-detect.html`, `/connecttest.txt` 등 | 캡티브 포털 감지 → 302 `/` (AP 모드에서만) |

## 2. 시스템
| 메서드 | 경로 | 요청 | 응답 |
|---|---|---|---|
| GET | `/api/system/info` | | `{device_id, fw, build, uptime_s, configured, heap_free, heap_min, ip, ap_ip, rssi, wifi_state, role, mode, base_topic, mqtt_state, mqtt_broker, mqtt_address_source, mqtt_rc, boot_reason, broker:{…broker 역할일 때}, slots:[{index,type,slug,enabled,online,errors}]}` |
| GET | `/api/system/modes` | | `{current, modes:[{id,label,role,slug,slave_id,baud}]}` — 동작 모드 목록 (→ [15-roles.md](15-roles.md) §1). `display` 항목은 `supported`(이 빌드에 패널 드라이버가 있나)·`panels` 포함 |
| POST | `/api/system/mode` | `{"mode":"broker"\|"display"\|"upower"\|"jbdbms"\|"rtusw_mk1"\|"rtusw_mk2"\|"mach"\|"idle", "config"?:{부분 설정}, "restart"?:bool}` | `202 {"ok":true,"mode":…,"restart_required":bool}`. role·slots·ports·base_topic을 한 번에 맞춘다. `config`(wifi·display 등)는 같은 검증을 거쳐 함께 적용, `restart:true`면 적용 후 재부팅(최초 설정 마법사). 역할이 바뀌면 적용 후 자동 재부팅 |
| POST | `/api/system/restart` | | `202 {"ok":true}` 후 500ms 뒤 재부팅 |
| POST | `/api/system/factory_reset` | `{"confirm":"RESET"}` | 설정 삭제 → 재부팅 |
| POST | `/api/system/rediscover` | | Discovery 전체 재발행 |
| POST | `/api/system/legacy_cleanup` | | 레거시 config 토픽 삭제 발행 (1회) |
| GET | `/api/system/log?since=<seq>` | | `{next:<seq>, lines:[{seq,ms,level,msg}]}` 링버퍼 200줄 |
| GET | `/api/mqtt?since=<seq>` | | 실시간 MQTT 뷰. Node: `{role, client:{state, broker, port, address_source, mdns_name, client_id, base_topic, last_rc, connected_s, published, failed, received}}` / Broker: `{role, broker:{…/api/system/info의 broker, client_list:[{id, connected_s}]}}`. 공통으로 `next:<seq>, messages:[{seq, ms, dir:"tx"\|"rx", topic, payload, size, retain?, failed?}]` — 링버퍼 50개, 페이로드는 159바이트까지 잘라 보관(`size`는 원래 길이). Node는 자신이 발행/구독한 메시지만, Broker는 버스 전체를 본다 |
| GET | `/api/system/scan` | | WiFi 스캔 `[{ssid,rssi,secure}]` (비동기, 진행 중이면 `202`) |

## 3. 설정
| 메서드 | 경로 | 요청 | 응답 |
|---|---|---|---|
| GET | `/api/config` | | 전체 설정(비밀번호 마스킹) |
| PUT | `/api/config` | 전체 설정 JSON | 핸들러에서 검증만 수행 → `202 {"ok":true,"queued":true}` (저장·적용은 메인 루프) / `400 {"error":"validation",...}` / `409` 다른 작업 대기 중. 적용 결과는 `/api/system/log`로 확인 |
| PATCH | `/api/config/<section>` | `wifi` / `mqtt` / `web` / `ports` / `slots` / `io` / `device` 부분 JSON | 동일 |
| GET | `/api/config/export?secrets=0|1` | | 다운로드용(`Content-Disposition: attachment; filename=essio-<id>.json`) |
| POST | `/api/config/import` | 전체 JSON | 검증 후 저장, 재부팅 필요 시 표기 |
| GET | `/api/config/schema` | | 타입별 params 기본값·채널 수 한계·모듈 목록 (`{types:[{type,label,max_channels,params_default}]}`) — UI가 폼을 동적으로 구성 |

## 4. 슬롯 상태·제어
| 메서드 | 경로 | 응답 |
|---|---|---|
| GET | `/api/slots` | `[{index,type,slug,label,enabled,online,last_ok_ms_ago,errors,state:{...toJson},switches:[{name,label,on}]}]` |
| GET | `/api/slots/{i}` | 단일 |
| POST | `/api/slots/{i}/switch/{name}` | 본문 `{"on":true}` → `202 {"queued":true}`; 알 수 없는 name → 404 |
| POST | `/api/slots/{i}/poll` | 본문 `{}` (JSON 필수) → 즉시 폴링 예약 → 202 |

`POST /api/slots/...` 는 하나의 JSON 핸들러(접두 매칭)가 처리하므로 본문은 항상 JSON이어야 한다.

`state` 스키마는 `07-mqtt-homeassistant.md` B.5 (MQTT 페이로드와 동일 객체).

## 5. 포트 진단
| 메서드 | 경로 | 요청 | 응답 |
|---|---|---|---|
| POST | `/api/ports/{id}/modbus` | `{"slave":10,"fc":4,"addr":13568,"count":19}` | `{"ok":true,"regs":[...]}` 또는 `{"ok":false,"code":226}` — 설정 전 장치 응답 확인용. 포트 lock 획득 실패 시 409 |

## 6. UI 화면 구성 (단일 페이지, 탭)

### 6.0 동작 모드 (구현됨)
페이지 최상단에 "동작 모드" 드롭다운 + 적용 버튼. `GET /api/system/modes`로 목록을 그리고
`POST /api/system/mode`로 전환한다. 브로커 모드에서는 슬롯 카드 대신 브로커 카드(수신 포트,
접속 클라이언트 수, 보관 중 retained, 메시지·재생·거부·누락 카운터)를 보여준다.

### 6.0.1 MQTT 실시간 · 연결 설정 (구현됨)
- **MQTT 실시간** 카드: `GET /api/mqtt`를 1초 주기로 폴링. Node는 연결 상태·브로커 주소(찾은 경로)·마지막 오류 코드·발행/실패/수신 수, Broker는 수신 상태·접속 클라이언트 목록·카운터를 보여준다. 아래에 메시지 흐름(최근 200개, 토픽 필터, 일시정지).
- **연결 설정** 카드: `GET /api/config`로 채우고 섹션별로 `PUT /api/config`(부분 JSON)로 저장한다. Wi-Fi(라우터 SSID/비밀번호, 2순위·폴백 AP, 설정용 AP 비밀번호), Node면 MQTT 브로커 접속(mDNS 이름, 수동 주소, 포트, 계정, 토픽 접두, Discovery), Broker면 내장 브로커(포트, 계정, 최대 클라이언트). 비밀번호 칸은 `********`로 채워지며 그대로 두면 기존 값 유지.

### 6.1 Dashboard
- 상단 바: 장치 이름, IP, WiFi RSSI, MQTT 상태(색), 업타임.
- 슬롯 카드 ×(활성 슬롯): 타입 아이콘, 온라인 배지, 마지막 갱신 "n초 전", 오류 수.
  - Upower: PV/Grid/Inverter/Bypass/Battery 그룹별 V/A/W, kWh, 온도, SOC; 스위치 토글 4개.
  - Jbdbms: 팩 전압/전류/전력, SOC, 잔량/만충, 사이클, 셀 편차, 셀 전압 바 그래프(min/max 강조), NTC, 보호 상태 배지; 충/방전 FET 토글.
  - RtuSw: 채널 토글 목록(이름 표시).
- 3초 주기 `/api/slots` 폴링. 토글 클릭 → POST → 낙관적 갱신 → 다음 폴링에서 확정.

### 6.2 Slots
- 슬롯 0..3 각각: enabled, type 드롭다운, slug, label, port, slave_id, poll_interval.
- type 선택에 따라 params 폼 동적 생성(`/api/config/schema`).
  - rtusw: 채널 표(번호/이름/사용).
  - upower: 블록 체크박스, 재시도, 마스킹, 스토리지 모드(접힘).
  - jbdbms: 셀 수, 노출 옵션, 충전 제한.
- "장치 테스트" 버튼: `/api/ports/{id}/modbus` 로 슬레이브 응답 확인(Modbus 타입만).
- 저장 → `PATCH /api/config/slots`.

### 6.3 Network
- WiFi: SSID(스캔 버튼→목록), 비밀번호, 고정 IP 접힘, AP 설정 접힘.
- MQTT: host/port/user/pw, base_topic, discovery on/off + prefix, keepalive, legacy_topics.
- "연결 테스트"(MQTT): 저장 없이 임시 접속 시도 → 결과 코드 표시.

### 6.4 Ports & IO
- 포트 3개 표: kind/rx/tx/baud/de/timeout. 핀 중복 시 인라인 경고.
- 버튼/출력 표: 핀, 극성, 디바운스, 대상 슬롯/스위치(활성 슬롯의 스위치 목록에서 선택), 모드.

### 6.5 System
- 정보(device_id, fw, heap, boot reason), 로그 뷰(레벨 필터, 자동 스크롤), 로그 레벨 설정.
- 버튼: Rediscover, Legacy cleanup, Restart, Factory reset(확인 문자열 입력), Export/Import, (2차) OTA 업로드.
- 웹 인증 설정.

### 6.6 공통
- 저장 결과 토스트: 적용됨 / 재부팅 필요(버튼 제공).
- 모바일 폭 대응(카드 1열).
- 외부 리소스 없음(AP 모드 동작). JS/CSS 인라인, gzip 후 ≤ 60KB 목표.

## 7. 인증/보안
- Basic Auth 활성 시 `/`와 `/api/*` 전체 보호, 캡티브 감지 경로는 제외.
- 비밀번호 필드는 마스킹, 응답에 원문 미포함.
- CSRF: 브라우저 폼 아닌 JSON API + 커스텀 헤더 `X-Requested-With: essio` 요구.
- 요청 본문 ≤ 16KB.
