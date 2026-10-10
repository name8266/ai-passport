#pragma once
#include "lvgl.h"
typedef struct {
    uint32_t background, surface, surface_alt, primary, secondary;
    uint32_t accent, selected, success, danger, separator;
} hub_ui_palette_t;
LV_FONT_DECLARE(notification_hub_16);
LV_FONT_DECLARE(notification_hub_24);
bool hub_ui_font_check(const lv_font_t *font);
const hub_ui_palette_t *hub_ui_palette(bool dark);
void hub_ui_set_text(lv_obj_t *label,const char *text);
void hub_ui_set_text_fmt(lv_obj_t *label,const char *fmt,...);
