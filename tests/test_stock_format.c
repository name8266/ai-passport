#include "src/lv_conf_internal.h"
#include "src/stdlib/lv_sprintf.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if LV_USE_STDLIB_SPRINTF != LV_STDLIB_CLIB
#error Stock UI requires the standard formatter for floating point labels
#endif
int main(void) {
    char text[256];
    lv_snprintf(text, sizeof(text), "%.2f", 11.57);
    assert(!strcmp(text, "11.57"));
    lv_snprintf(text, sizeof(text), "%+.2f%%", -0.53);
    assert(!strcmp(text, "-0.53%"));
    lv_snprintf(text, sizeof(text), "%+.2f%%", 1.94);
    assert(!strcmp(text, "+1.94%"));
    lv_snprintf(text, sizeof(text), "%s  收 %.2f\n开 %.2f  高 %.2f  低 %.2f\n量 %.0f手 · %s",
                "2026-09-30", 11.57, 11.35, 11.60, 11.30, 23456.0, "MA5/10/20");
    assert(strstr(text, "收 11.57") && strstr(text, "量 23456手 · MA5/10/20"));
    lv_snprintf(text, sizeof(text), "%s %02u:%02u  %.2f\n昨收 %.2f · 累计 %.0f手", "2026-09-30",
                15u, 0u, 11.57, 11.35, 23456.0);
    assert(strstr(text, "15:00  11.57") && strstr(text, "昨收 11.35"));
    lv_snprintf(text, sizeof(text), "%s · %.2s-%.2s", "000001", "0930", "30");
    assert(!strcmp(text, "000001 · 09-30"));
    puts("Stock LVGL formatter: PASS (prices, signed percentages, candles with trailing string, "
         "intraday, precision strings)");
    return 0;
}
