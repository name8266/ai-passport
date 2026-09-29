// FoloToy AI Passport 离线塔罗应用。
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "tarot_audio.h"
#include "tarot_catalog.h"
#include "tarot_history.h"
#include "tarot_model.h"
#include "tarot_runtime.h"
#include "tarot_store.h"

LV_FONT_DECLARE(tarot_font_14);
LV_FONT_DECLARE(tarot_font_20);

#define COLOR_NIGHT 0x100D24
#define COLOR_PANEL 0x211A3D
#define COLOR_PANEL_SELECTED 0x392A5B
#define COLOR_GOLD 0xD7B66B
#define COLOR_IVORY 0xF5EBD5
#define COLOR_MUTED 0xA99EBC
#define INPUT_QUEUE_DEPTH 10

typedef enum {
    PAGE_HOME = 0, PAGE_SPREADS, PAGE_READING, PAGE_DETAIL,
    PAGE_LIBRARY, PAGE_HISTORY, PAGE_SETTINGS, PAGE_CONFIRM_CLEAR, PAGE_ABOUT,
} app_page_t;

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} input_event_t;

static const char *TAG = "tarot_app";
static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;
static app_page_t s_page = PAGE_HOME;
static lv_obj_t *s_screen;
static lv_obj_t *s_battery_label;
static tarot_persisted_t s_data;
static tarot_session_t s_session;
static lv_image_dsc_t s_card_image;
static bool s_session_saved;
static bool s_save_blocked;
static bool s_audio_available;
static bool s_battery_available;
static uint8_t s_home_index;
static uint8_t s_spread_index;
static uint8_t s_settings_index;
static uint8_t s_confirm_index;
static uint8_t s_library_card;
static uint8_t s_history_index;
static int s_battery_soc = -1;
static tarot_runtime_t s_runtime;
static char s_text_buffer[640];

static const char *const HOME_ITEMS[] = {
    "开始占卜", "今日指引", "牌库百科", "历史记录", "设置", "关于",
};
static const char *const SPREAD_ITEMS[] = { "单牌问答", "三牌阵", "凯尔特十字" };

static lv_obj_t *new_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                           lv_color_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *build_base(const char *title) {
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_battery_label = NULL;
    }
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_NIGHT), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_t *heading = new_label(s_screen, title, &tarot_font_20, lv_color_hex(COLOR_GOLD));
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 14, 12);
    char battery[12];
    if (s_battery_soc >= 0) snprintf(battery, sizeof(battery), "%d%%", s_battery_soc);
    else snprintf(battery, sizeof(battery), "--%%");
    s_battery_label = new_label(s_screen, battery, &tarot_font_14, lv_color_hex(COLOR_MUTED));
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -14, 16);
    lv_obj_t *line = lv_obj_create(s_screen);
    lv_obj_set_size(line, 212, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_60, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 42);
    return s_screen;
}

static void menu_box(const char *text, int x, int y, int width, int height, bool selected) {
    lv_obj_t *row = lv_obj_create(s_screen);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, width, height);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(selected ? COLOR_PANEL_SELECTED : COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(selected ? COLOR_GOLD : 0x4C4167), 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_t *label = new_label(row, text, &tarot_font_14,
                                lv_color_hex(selected ? COLOR_IVORY : COLOR_MUTED));
    lv_obj_center(label);
}
static void menu_row(const char *text, int y, bool selected) { menu_box(text, 13, y, 214, 38, selected); }

static void footer_hint(const char *text) {
    lv_obj_t *hint = new_label(s_screen, text, &tarot_font_14, lv_color_hex(COLOR_MUTED));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static void show_home(void) {
    s_page = PAGE_HOME;
    build_base("星图塔罗");
    for (size_t i = 0; i < 6; ++i) {
        int column = (int)(i % 2U), row = (int)(i / 2U);
        menu_box(HOME_ITEMS[i], 12 + column * 114, 58 + row * 64, 102, 54, i == s_home_index);
    }
    footer_hint("上下选择 · 确定进入");
    lv_screen_load(s_screen);
}

static void show_spreads(void) {
    s_page = PAGE_SPREADS;
    build_base("选择牌阵");
    for (size_t i = 0; i < 3; ++i) menu_row(SPREAD_ITEMS[i], 72 + (int)i * 50, i == s_spread_index);
    lv_obj_t *note = new_label(s_screen, "请先在心中明确问题，再开始洗牌。", &tarot_font_14,
                               lv_color_hex(COLOR_MUTED));
    lv_obj_set_width(note, 210);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -44);
    footer_hint("长按确定返回");
    lv_screen_load(s_screen);
}

static void show_card_back(void) {
    lv_obj_t *back = lv_obj_create(s_screen);
    lv_obj_set_size(back, TAROT_IMAGE_WIDTH, TAROT_IMAGE_HEIGHT);
    lv_obj_align(back, LV_ALIGN_CENTER, 0, 3);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x26184B), 0);
    lv_obj_set_style_border_width(back, 3, 0);
    lv_obj_set_style_border_color(back, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_t *mark = new_label(back, "星\n图\n塔\n罗", &tarot_font_20, lv_color_hex(COLOR_GOLD));
    lv_obj_set_style_text_align(mark, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(mark);
}

static void show_reading(void) {
    s_page = PAGE_READING;
    build_base(tarot_spread_name(s_session.spread));
    tarot_draw_t *draw = &s_session.cards[s_session.selected];
    snprintf(s_text_buffer, sizeof(s_text_buffer), "%s  %u/%u",
             tarot_position_name(s_session.spread, s_session.selected),
             (unsigned)s_session.selected + 1, (unsigned)s_session.count);
    lv_obj_t *position = new_label(s_screen, s_text_buffer, &tarot_font_14, lv_color_hex(COLOR_IVORY));
    lv_obj_align(position, LV_ALIGN_TOP_MID, 0, 50);
    if (!draw->revealed) {
        show_card_back();
        footer_hint("确定揭牌 · 上下换牌");
    } else if (tarot_card_image(draw->card_id, &s_card_image)) {
        lv_obj_t *image = lv_image_create(s_screen);
        lv_image_set_src(image, &s_card_image);
        lv_obj_align(image, LV_ALIGN_CENTER, 0, 3);
        if (draw->reversed) {
            lv_image_set_pivot(image, TAROT_IMAGE_WIDTH / 2, TAROT_IMAGE_HEIGHT / 2);
            lv_image_set_rotation(image, 1800);
        }
        snprintf(s_text_buffer, sizeof(s_text_buffer), "%s%s",
                 tarot_card_name(draw->card_id), draw->reversed ? " · 逆位" : " · 正位");
        lv_obj_t *name = new_label(s_screen, s_text_buffer, &tarot_font_14, lv_color_hex(COLOR_GOLD));
        lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -35);
        footer_hint(s_save_blocked ? "无法保存 · 收藏保护" : "确定看解读 · 上下换牌");
    }
    lv_screen_load(s_screen);
}

static void show_detail(void) {
    s_page = PAGE_DETAIL;
    tarot_draw_t *draw = &s_session.cards[s_session.selected];
    build_base(tarot_card_name(draw->card_id));
    tarot_card_interpret(draw->card_id, draw->reversed,
                         tarot_position_name(s_session.spread, s_session.selected),
                         s_text_buffer, sizeof(s_text_buffer));
    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_set_pos(panel, 13, 55);
    lv_obj_set_size(panel, 214, 220);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_t *body = new_label(panel, s_text_buffer, &tarot_font_14, lv_color_hex(COLOR_IVORY));
    lv_obj_set_width(body, 184);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(body, 7, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 8);
    footer_hint("长按确定返回牌面");
    lv_screen_load(s_screen);
}

static void show_library(void) {
    s_page = PAGE_LIBRARY;
    build_base("牌库百科");
    if (tarot_card_image(s_library_card, &s_card_image)) {
        lv_obj_t *image = lv_image_create(s_screen);
        lv_image_set_src(image, &s_card_image);
        lv_obj_align(image, LV_ALIGN_CENTER, 0, 0);
    }
    snprintf(s_text_buffer, sizeof(s_text_buffer), "%02u/78  %s",
             (unsigned)s_library_card + 1, tarot_card_name(s_library_card));
    lv_obj_t *name = new_label(s_screen, s_text_buffer, &tarot_font_14, lv_color_hex(COLOR_GOLD));
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -34);
    footer_hint("上下浏览 · 长按返回");
    lv_screen_load(s_screen);
}

static void show_history(void) {
    s_page = PAGE_HISTORY;
    build_base("历史记录");
    if (s_data.history.count == 0) {
        lv_obj_t *empty = new_label(s_screen, "还没有完成的占卜。\n完成揭牌后会自动保存。",
                                    &tarot_font_14, lv_color_hex(COLOR_MUTED));
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, -5);
    } else {
        if (s_history_index >= s_data.history.count) s_history_index = 0;
        const tarot_record_t *record = tarot_history_recent(&s_data.history, s_history_index);
        snprintf(s_text_buffer, sizeof(s_text_buffer), "第 %lu 次 · %s%s\n\n",
                 (unsigned long)record->sequence, tarot_spread_name(record->spread),
                 record->favorite ? " · 已收藏" : "");
        size_t used = strlen(s_text_buffer);
        for (size_t i = 0; i < record->count && i < 5; ++i) {
            used += snprintf(s_text_buffer + used, sizeof(s_text_buffer) - used,
                             "%s：%s%s\n", tarot_position_name(record->spread, i),
                             tarot_card_name(record->cards[i].card_id),
                             record->cards[i].reversed ? "（逆）" : "");
        }
        if (record->count > 5) snprintf(s_text_buffer + used, sizeof(s_text_buffer) - used,
                                        "其余 %u 张…", (unsigned)record->count - 5U);
        lv_obj_t *body = new_label(s_screen, s_text_buffer, &tarot_font_14, lv_color_hex(COLOR_IVORY));
        lv_obj_set_width(body, 205);
        lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 63);
    }
    footer_hint(s_data.history.count ? "上下翻阅 · 确定收藏 · 长按返回" : "长按确定返回");
    lv_screen_load(s_screen);
}

static void show_settings(void) {
    s_page = PAGE_SETTINGS;
    build_base("设置");
    char rows[4][40];
    snprintf(rows[0], sizeof(rows[0]), "逆位：%s", s_data.reversals_enabled ? "开启" : "关闭");
    snprintf(rows[1], sizeof(rows[1]), "声音：%s", s_data.sound_enabled && s_audio_available ? "开启" : "关闭");
    snprintf(rows[2], sizeof(rows[2]), "亮度：%u%%", s_data.brightness);
    snprintf(rows[3], sizeof(rows[3]), "清理非收藏历史");
    for (size_t i = 0; i < 4; ++i) menu_row(rows[i], 68 + (int)i * 48, i == s_settings_index);
    footer_hint(tarot_store_has_error() ? "保存不可用 · 长按返回" : "确定修改 · 长按返回");
    lv_screen_load(s_screen);
}
static void show_confirm_clear(void) {
    s_page = PAGE_CONFIRM_CLEAR;
    build_base("清理历史");
    lv_obj_t *body = new_label(s_screen, "确定清理非收藏历史?\n\n已收藏记录会保留。", &tarot_font_14, lv_color_hex(COLOR_IVORY));
    lv_obj_set_width(body, 205); lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 78);
    menu_row("返回", 168, s_confirm_index == 0); menu_row("确定清理", 216, s_confirm_index == 1);
    footer_hint("上下选择 · 确定"); lv_screen_load(s_screen);
}
static void show_about(void) {
    s_page = PAGE_ABOUT;
    build_base("关于");
    const char *text = "星图塔罗 · 离线版\n\n78 张 1909 Waite-Smith 公版牌面。所有解读均在设备本地完成。\n\n本应用用于娱乐与自我反思，不替代医疗、法律、财务或心理专业意见。";
    lv_obj_t *body = new_label(s_screen, text, &tarot_font_14, lv_color_hex(COLOR_IVORY));
    lv_obj_set_width(body, 205);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(body, 5, 0);
    lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 64);
    footer_hint("长按确定返回");
    lv_screen_load(s_screen);
}

static void start_reading(tarot_spread_t spread) {
    if (!tarot_session_start(&s_session, spread, s_data.reversals_enabled, esp_random)) return;
    s_session_saved = false;
    s_save_blocked = false;
    tarot_audio_play_confirm();
    show_reading();
}

static void save_completed_session(void) {
    if (s_session_saved || !tarot_session_all_revealed(&s_session)) return;
    if (tarot_history_add(&s_data.history, &s_session)) {
        tarot_store_request_save(&s_data); s_session_saved = true; s_save_blocked = false; return;
    }
    s_save_blocked = tarot_history_full_with_favorites(&s_data.history);
}

static void handle_long_back(void) {
    if (s_page == PAGE_HOME) return;
    if (s_page == PAGE_DETAIL) show_reading();
    else if (s_page == PAGE_CONFIRM_CLEAR) show_settings();
    else show_home();
}

static void handle_click(bsp_btn_t button) {
    int delta = button == BSP_BTN_UP ? -1 : button == BSP_BTN_DOWN ? 1 : 0;
    if (s_page == PAGE_HOME) {
        if (delta) s_home_index = (uint8_t)((s_home_index + 6 + delta) % 6);
        else if (button == BSP_BTN_OK) {
            if (s_home_index == 0) show_spreads();
            else if (s_home_index == 1) start_reading(TAROT_SPREAD_DAILY);
            else if (s_home_index == 2) show_library();
            else if (s_home_index == 3) show_history();
            else if (s_home_index == 4) show_settings();
            else show_about();
            return;
        }
        show_home();
    } else if (s_page == PAGE_SPREADS) {
        if (delta) s_spread_index = (uint8_t)((s_spread_index + 3 + delta) % 3);
        else if (button == BSP_BTN_OK) {
            static const tarot_spread_t spreads[] = { TAROT_SPREAD_SINGLE, TAROT_SPREAD_THREE, TAROT_SPREAD_CELTIC_CROSS };
            start_reading(spreads[s_spread_index]);
            return;
        }
        show_spreads();
    } else if (s_page == PAGE_READING) {
        if (delta) tarot_session_move(&s_session, delta);
        else if (button == BSP_BTN_OK) {
            tarot_draw_t *draw = &s_session.cards[s_session.selected];
            if (!draw->revealed) {
                tarot_session_reveal(&s_session);
                tarot_audio_play_reveal();
                save_completed_session();
            } else {
                show_detail();
                return;
            }
        }
        show_reading();
    } else if (s_page == PAGE_LIBRARY) {
        if (delta) s_library_card = (uint8_t)((s_library_card + TAROT_CARD_COUNT + delta) % TAROT_CARD_COUNT);
        show_library();
    } else if (s_page == PAGE_HISTORY) {
        if (s_data.history.count && delta) s_history_index = (uint8_t)((s_history_index + s_data.history.count + delta) % s_data.history.count);
        else if (s_data.history.count && button == BSP_BTN_OK) {
            const tarot_record_t *record = tarot_history_recent(&s_data.history, s_history_index);
            if (record) {
                tarot_history_toggle_favorite(&s_data.history, record->sequence);
                tarot_store_request_save(&s_data);
            }
        }
        show_history();
    } else if (s_page == PAGE_SETTINGS) {
        if (delta) s_settings_index = (uint8_t)((s_settings_index + 4 + delta) % 4);
        else if (button == BSP_BTN_OK) {
            if (s_settings_index == 0) s_data.reversals_enabled = !s_data.reversals_enabled;
            else if (s_settings_index == 1 && s_audio_available) {
                s_data.sound_enabled = !s_data.sound_enabled;
                tarot_audio_set_enabled(s_data.sound_enabled);
            } else if (s_settings_index == 2) {
                s_data.brightness = s_data.brightness >= 100 ? 40 : s_data.brightness + 30;
                bsp_display_backlight(s_data.brightness);
            } else if (s_settings_index == 3) {
                s_confirm_index = 0; show_confirm_clear(); return;
            }
            tarot_store_request_save(&s_data);
        }
        show_settings();
    } else if (s_page == PAGE_CONFIRM_CLEAR) {
        if (delta) s_confirm_index = (uint8_t)((s_confirm_index + 2 + delta) % 2);
        else if (button == BSP_BTN_OK) {
            if (s_confirm_index == 1) { tarot_history_clear_nonfavorites(&s_data.history); s_history_index = 0; tarot_store_request_save(&s_data); }
            show_settings(); return;
        }
        show_confirm_clear();
    }
}
static void apply_idle_state(tarot_idle_state_t state, tarot_idle_state_t previous) {
    if (state == TAROT_IDLE_OFF) { bsp_display_backlight(0); tarot_audio_suspend(); }
    else if (state == TAROT_IDLE_DIMMED) bsp_display_backlight(s_data.brightness > 20 ? 15 : s_data.brightness);
    else { bsp_display_backlight(s_data.brightness); if (previous == TAROT_IDLE_OFF) tarot_audio_resume(); }
}
static void refresh_battery_if_due(int64_t now_us) {
    if (!s_battery_available || !tarot_runtime_battery_refresh_due(&s_runtime, now_us)) return;
    int soc = bsp_battery_soc(); if (soc < 0 || soc == s_battery_soc) return; s_battery_soc = soc;
    if (!bsp_lvgl_lock(100)) return;
    if (s_battery_label) { char battery[12]; snprintf(battery, sizeof(battery), "%d%%", s_battery_soc); lv_label_set_text(s_battery_label, battery); }
    bsp_lvgl_unlock();
}
static void input_task(void *argument) {
    (void)argument; input_event_t input;
    for (;;) {
        if (xQueueReceive(s_input_queue, &input, pdMS_TO_TICKS(500)) != pdTRUE) {
            int64_t now_us=esp_timer_get_time(); tarot_idle_state_t previous=s_runtime.idle_state;
            tarot_idle_state_t state=tarot_runtime_update_idle(&s_runtime, now_us);
            if (state != previous) apply_idle_state(state, previous); refresh_battery_if_due(now_us); continue;
        }
        int64_t now_us=esp_timer_get_time(); tarot_idle_state_t previous=s_runtime.idle_state; bool woke=false;
        bool dispatch=tarot_runtime_note_input(&s_runtime, now_us, input.event==BSP_BTN_PRESS, &woke);
        if (previous != TAROT_IDLE_ACTIVE) apply_idle_state(TAROT_IDLE_ACTIVE, previous);
        if (!dispatch || !bsp_lvgl_lock(500)) continue;
        if (input.event == BSP_BTN_LONG && input.button == BSP_BTN_OK) handle_long_back();
        else if (input.event == BSP_BTN_CLICK) handle_click(input.button);
        bsp_lvgl_unlock();
    }
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (!s_input_ready || !s_input_queue) return;
    input_event_t input = { .button = button, .event = event };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "starting offline tarot application");
    (void)bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    (void)tarot_store_init(&s_data);
    s_audio_available = tarot_audio_init();
    if (!s_audio_available) s_data.sound_enabled = false;
    tarot_audio_set_enabled(s_data.sound_enabled);
    if (bsp_battery_init() == ESP_OK) { s_battery_available = true; s_battery_soc = bsp_battery_soc(); }
    bsp_display_backlight(s_data.brightness);
    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue || xTaskCreate(input_task, "tarot_input", 5120, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "input task allocation failed");
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button init failed");
        return;
    }
    tarot_runtime_init(&s_runtime, esp_timer_get_time());
    if (bsp_lvgl_lock(1000)) {
        show_home();
        bsp_lvgl_unlock();
        s_input_ready = true;
    }
    ESP_LOGI(TAG, "ready: audio=%d battery=%d persistence_error=%d",
             s_audio_available, s_battery_soc, tarot_store_has_error());
}
