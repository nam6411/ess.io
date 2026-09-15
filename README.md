# ess.io2

ESP32 기반 ESS(태양광/인버터/배터리) Modbus·UART → MQTT(Home Assistant) 게이트웨이의 **재구현 프로젝트**.

기존 `ess.io`(ESP8266/ESP32 겸용, 모든 장치가 `main.cpp`에 하드코딩)를 분석하여 스펙을 추출하고,
아래 목표로 새로 작성한다.

- ESP32 전용
- 장치 모듈(Upower / Jbdbms / RtuSwMk1 / RtuSwMk2) 중 하나를 **웹 UI에서 선택·설정**
- WiFi / MQTT 접속 정보를 웹 UI에서 입력, 저장, 즉시 적용
- 각 모듈이 읽은 데이터를 설정된 MQTT 브로커로 Home Assistant Discovery 규격으로 발행

## 문서

`docs/` 폴더 참조. 읽는 순서는 [docs/00-overview.md](docs/00-overview.md)의 목차를 따른다.

## 소스 구조

```
platformio.ini            esp32dev (Arduino core 2.0.x) + native 테스트 환경
partitions.csv            OTA 2슬롯 + LittleFS 960KB
data/index.html           단일 장치 대시보드·설정 UI (pio run -t uploadfs)
src/
  main.cpp                조립·메인 루프
  core/                   Config, Logger, NetManager, MqttManager, Scheduler, HaDiscovery, IoManager, WebApi, SysInfo
  port/                   SerialPort(HW UART/SoftwareSerial + 뮤텍스), ModbusRtu(자체 구현), Crc16
  modules/                IDeviceModule, ModuleRegistry, upower/, jbdbms/, rtusw/
test/test_frames/         CRC16·JBD 프레임 단위 테스트 (pio test -e native)
```

## 빌드

PlatformIO는 `~/.platformio-venv`에 설치되어 있다 (`~/.platformio-venv/bin/pio`).

```
pio run                      # 펌웨어
pio run -t uploadfs          # data/ → LittleFS
pio run -t upload            # 펌웨어 업로드
pio test -e native           # 순수 로직 테스트
```

## 첫 실행

1. 설정 파일이 없으면 UPower 단일 장치 기본 구성으로 부팅하고 AP `essio-<id>` (비밀번호 `12341234`)를 연다.
2. `http://192.168.4.1/`에서 장치·포트·MQTT·노출 센서를 설정한다.
3. STA 연결 후 `http://essio-<id>.local/`.

## 상태

- [x] 기존 코드 분석 및 스펙 문서화 (docs/)
- [x] 설정 저장소, 네트워크/MQTT 상태 머신, 단일 장치 스케줄러, Modbus RTU, 모듈 4종, REST API, 설정 UI
- [x] ESP32 타겟 빌드 검증 (Flash 64%, RAM 20%) + native 테스트 5/5
- [ ] export/import, WiFi 스캔, 포트 진단 API
- [x] Discovery 삭제 토픽 기록 및 설정 변경 시 retained 엔티티 정리
- [ ] Jbdbms 충전 제한
- [ ] OTA
