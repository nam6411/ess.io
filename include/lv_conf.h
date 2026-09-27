// LVGL 9 설정 — Display 역할(esp32s3 빌드)에서만 쓴다. 적지 않은 항목은 lv_conf_internal.h 기본값.
// Elecrow CrowPanel 2.1 공식 예제의 lv_conf.h에서 필요한 부분만 옮겼다.
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// 메모리는 C 라이브러리 malloc을 쓴다 (큰 할당은 PSRAM으로 간다)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 33
#define LV_USE_OS LV_OS_NONE  // LVGL 호출은 메인 루프 한 곳에서만
#define LV_USE_LOG 0

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif
#endif
