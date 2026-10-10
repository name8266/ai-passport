#pragma once
/* ESP32-C3 internal SRAM protections; allocation-free and host-testable.
 * These are MINIMA, not performance claims. The runtime telemetry reports
 * true post-startup heap and historic low-water observations on hardware.
 */
#include <stdbool.h>
#include <stddef.h>
#define HUB_MEM_WARN_BYTES 16384u
#define HUB_MEM_WEB_START_BYTES 40960u
#define HUB_MEM_WEB_CONTIGUOUS_BYTES 16384u
#define HUB_MEM_TLS_START_BYTES 49152u
#define HUB_MEM_TLS_CONTIGUOUS_BYTES 20480u
static inline bool hub_mem_allow_web(size_t free_bytes,size_t largest_bytes) {
    return free_bytes>=HUB_MEM_WEB_START_BYTES &&
           largest_bytes>=HUB_MEM_WEB_CONTIGUOUS_BYTES;
}
static inline bool hub_mem_allow_tls(size_t free_bytes,size_t largest_bytes) {
    return free_bytes>=HUB_MEM_TLS_START_BYTES &&
           largest_bytes>=HUB_MEM_TLS_CONTIGUOUS_BYTES;
}
/* Post-initialization safe floor: roll back optional Web immediately if its
 * allocations consume the remaining BLE/Flash reserve. */
static inline bool hub_mem_web_running_safe(size_t free_bytes,size_t largest) {
    return free_bytes>=HUB_MEM_WARN_BYTES && largest>=4096u;
}
static inline bool hub_mem_critical(size_t free_bytes) {
    return free_bytes<HUB_MEM_WARN_BYTES;
}
