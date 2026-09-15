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
| GET | `/api/system/info` | | `{device_id, fw, build, uptime_s, heap_free, heap_min, ip, ap_ip, rssi, wifi_state, mqtt_state, mqtt_connected_since, boot_reason, slots:[{index,type,slug,enabled,online,last_ok_ms_ago,errors}]}` |
| POST | `/api/system/restart` | | `202 {"ok":true}` 후 500ms 뒤 재부팅 |
| POST | `/api/system/factory_reset` | `{"confirm":"RESET"}` | 설정 삭제 → 재부팅 |
| POST | `/api/system/rediscover` | | Discovery 전체 재발행 |
| POST | `/api/system/legacy_cleanup` | | 레거시 config 토픽 삭제 발행 (1회) |
| GET | `/api/system/log?since=<seq>` | | `{next:<seq>, lines:[{seq,ms,level,msg}]}` 링버퍼 200줄 |
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

### 6.1 Dashboard
- 상단 바: 장치 이름, IP, WiFi RSSI, MQTT 상태(색), 업타임.
- 슬롯 카드 ×(활성 슬롯): 타입 아이콘, 온라인 배지, 마지막 갱신 "n초 전", 오류 수.
  - Upower: PV/Grid/Inverter/Bypass/Battery 그룹별 V/A/W, kWh, 온도, SOC; 스위치 토글 4개.
  - Jbdbms: 팩 전압/전류/전력, SOC, 잔량/만충, 사이클, 셀 편차, 셀 전압 바 그래프(min/max 강조), NTC, 보호 상태 배지; 충/방전 FET 토글.
  - RtuSw: 채널 토글 목록(이름 표시).
- 3초 주기 `/api/slots` 폴링. 토글 클릭 → POST → 낙관적 갱신 → 다음 폴링에서 확정.

### 6.2 Device
- 단일 장치: enabled, type 드롭다운, slug, label, slave_id, poll_interval.
- type 선택에 따라 params 폼 동적 생성(`/api/config/schema`).
  - rtusw: 채널 표(번호/이름/사용).
  - upower: 블록 체크박스, 재시도, 실제 바이패스 상태 기반 마스킹.
  - jbdbms: 셀 수, 노출 옵션, 충전 제한.
- "장치 테스트" 버튼: `/api/ports/{id}/modbus` 로 슬레이브 응답 확인(Modbus 타입만).
- 포트와 MQTT 설정을 함께 편집하고 저장 → `PUT /api/config`.

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
