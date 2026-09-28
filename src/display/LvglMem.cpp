#if ESSIO_DISPLAY
// LVGL 메모리를 PSRAM에 둔다 (include/lv_conf.h: LV_USE_STDLIB_MALLOC = LV_STDLIB_CUSTOM).
// C 라이브러리 malloc은 작은 할당을 내부 RAM에 두는데, 링(arc)·라벨 수백 개와 그리기용 임시 버퍼가
// 내부 힙을 30KB대까지 깎아 TCP 송신이 실패하고 MQTT가 끊겼다. 화면 쪽은 PSRAM으로도 충분히 빠르다.
#include <esp_heap_caps.h>
#include <lvgl.h>

extern "C" {

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void*, size_t) { return nullptr; }
void lv_mem_remove_pool(lv_mem_pool_t) {}

void* lv_malloc_core(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void* lv_realloc_core(void* p, size_t size) { return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void lv_free_core(void* p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon) {
    // LVGL의 자체 모니터 대신 PSRAM 힙 통계를 대략 채운다
    lv_memzero(mon, sizeof(*mon));
    mon->total_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    mon->free_size = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    mon->free_biggest_size = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

}  // extern "C"
#endif
