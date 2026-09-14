# 02. 하드웨어 스펙

> 기존 `main.cpp` ESP32 분기 + `common.h` 기준. 신규 설계에서 변경 가능한 부분은 "신규" 열에 표시.

## 1. MCU

- **ESP32** (PlatformIO `esp32dev`, Arduino 프레임워크). ESP8266 지원은 신규에서 제외.
- 플래시/PSRAM 요구사항 없음 (기본 4MB 플래시로 충분).

## 2. 핀맵 (기존 ESP32)

| 기능 | 방향 | GPIO | 파라미터 | 신규 |
|---|---|---|---|---|
| UPOWER RS485 RX | IN | 22 | 115200 8N1 | 설정 가능(포트 A) |
| UPOWER RS485 TX | OUT | 23 | | |
| BMS UART RX | IN | 16 | 9600 8N1 | 설정 가능(포트 B) |
| BMS UART TX | OUT | 17 | | |
| RTU 스위치 RS485 RX | IN | 18 | 9600 8N1 | 설정 가능(포트 C) |
| RTU 스위치 RS485 TX | OUT | 19 | | |
| 인버터 버튼 | IN (PULLUP) | 12 | FALLING 인터럽트 | 설정 가능 |
| 무버 버튼 | IN (PULLUP) | 14 | FALLING 인터럽트 | 설정 가능 |
| 인버터 상태 LED/릴레이 | OUT | 25 | HIGH=ON | 설정 가능 |
| 무버 상태 LED/릴레이 | OUT | 26 | HIGH=ON | 설정 가능 |
| 디버그 콘솔 | UART0 | 1/3 | 115200 | 유지 |

- GPIO12는 ESP32 부팅 스트래핑 핀(MTDI, 플래시 전압 선택). 외부 풀업 상태로 부팅 시 3.3V 플래시 모듈에서 부팅 실패 가능 → **신규에서는 버튼 기본 핀을 다른 GPIO로 변경 권장** (예: 32/33).
- 기존 코드는 3포트 모두 `EspSoftwareSerial`을 사용. ESP32에는 하드웨어 UART가 3개(UART0 콘솔, UART1, UART2) 있으므로 신규는 **UART1/UART2를 우선 사용**하고 세 번째 포트만 SoftwareSerial로 처리하거나, 포트 수를 설정으로 제한.

## 3. RS485 트랜시버

- 기존 코드에 DE/RE 방향 제어 핀이 **없음**. pre/postTransmission 콜백은 `SoftwareSerial::enableIntTx()`만 토글한다.
  → 자동 방향 전환(auto-direction) RS485 모듈을 사용하고 있는 것으로 추정.
- 신규: 포트 설정에 `de_pin` (선택, -1이면 미사용)을 두어 수동 방향 제어 모듈도 지원.

## 4. 시리얼 파라미터 요약

| 장치 | 보레이트 | 프레이밍 | 프로토콜 | 슬레이브 주소 |
|---|---|---|---|---|
| UPOWER | 115200 | 8N1 | Modbus RTU | 10 (0x0A) |
| JBD BMS | 9600 | 8N1 | JBD 0xDD 프레임 | (없음; 토픽 번호용 0) |
| RTU SW Mk1 | 9600 | 8N1 | Modbus RTU | 실제 송신 255(0xFF), 토픽 번호 0 |
| RTU SW Mk2 | 9600 | 8N1 | Modbus RTU | 설정값 |

- Modbus 응답 타임아웃: ModbusMaster 기본 `ku16MBResponseTimeout = 2000ms`. 기존 코드는 변경하지 않음.
- SoftwareSerial 수신 버퍼 256바이트.

## 5. 버튼 / LED 전기 사양

- 버튼: 내부 풀업, 눌리면 GND. 하드웨어 디바운스 없음(기존은 채터링 엣지 수를 임계값으로 사용).
- 상태 출력: 3.3V 로직 HIGH = ON. LED 직결 시 저항 필요, 릴레이 구동 시 드라이버 필요.

## 6. 전원

명시된 요구사항 없음. RS485 모듈 3개 + WiFi 동시 동작 기준 5V/1A 이상 권장.
