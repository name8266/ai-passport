#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "zuma_art.h"
#include "zuma_audio.h"
#include "zuma_game.h"
#include "key_guard.h"
#include <math.h>
#include <string.h>

LV_FONT_DECLARE(zuma_cjk_14);
LV_FONT_DECLARE(zuma_cjk_16);
LV_FONT_DECLARE(zuma_cjk_20);

static const char *TAG = "star_orbit";
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_message_t;
static QueueHandle_t s_keys;
static zuma_game_t s_game;
static lv_obj_t *s_screen, *s_board, *s_subtitle, *s_score, *s_battery, *s_prompt, *s_progress;
static bool s_audio_ok;
static volatile bool s_input_ready;
static volatile int s_battery_soc = -1;
static key_guard_t s_key_guard;
static int s_held_dir;
static float s_repeat_clock;
static volatile uint32_t s_rendered_frames;
static volatile uint32_t s_flushed_pixels;

typedef struct {
    int16_t x, y;
    uint8_t color;
    bool visible;
} gem_frame_t;
typedef struct {
    gem_frame_t balls[ZUMA_MAX_BALLS];
    gem_frame_t shot;
    uint8_t current_color, next_color;
    int16_t aim_x, aim_y;
} board_frame_t;
static board_frame_t s_previous_frame;
static bool s_previous_frame_valid;

static void render_event(lv_event_t *event) {
    if (s_game.mode == ZUMA_PLAYING) s_rendered_frames++;
    (void)event;
}
static void flush_event(lv_event_t *event) {
    if (s_game.mode != ZUMA_PLAYING) return;
    const lv_area_t *area = lv_event_get_param(event);
    if (area) s_flushed_pixels += (uint32_t)lv_area_get_width(area) * lv_area_get_height(area);
}

static int px(float f) { return (int)lroundf(f); }
static void line(lv_layer_t *layer, int x1, int y1, int x2, int y2,
                 int width, uint32_t color) {
    lv_area_t bounds = {
        .x1 = (x1 < x2 ? x1 : x2) - width,
        .y1 = (y1 < y2 ? y1 : y2) - width,
        .x2 = (x1 > x2 ? x1 : x2) + width,
        .y2 = (y1 > y2 ? y1 : y2) + width,
    };
    const lv_area_t *clip = &layer->_clip_area;
    if (bounds.x2 < clip->x1 || bounds.x1 > clip->x2 ||
        bounds.y2 < clip->y1 || bounds.y1 > clip->y2) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
    d.width = width;
    d.color = lv_color_hex(color);
    d.opa = LV_OPA_COVER;
    d.round_start = 1; d.round_end = 1;
    lv_draw_line(layer, &d);
}
static void draw_image(lv_layer_t *layer, const lv_image_dsc_t *image, int x, int y) {
    lv_area_t a = {x, y, x + image->header.w - 1, y + image->header.h - 1};
    const lv_area_t *clip = &layer->_clip_area;
    if (a.x2 < clip->x1 || a.x1 > clip->x2 ||
        a.y2 < clip->y1 || a.y1 > clip->y2) return;
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src = image;
    lv_draw_image(layer, &d, &a);
}
static void gem(lv_layer_t *layer, int x, int y, uint8_t color) {
    /* Game colors are red, amber, jade, violet; sprite atlas starts with jade. */
    static const uint8_t image_index[4] = {2, 1, 0, 3};
    draw_image(layer, &zuma_marble_images[image_index[color % 4]], x - 7, y - 7);
}
static void invalidate_gem(const gem_frame_t *gem) {
    if (!gem->visible) return;
    lv_area_t area = {gem->x - 8, gem->y - 8, gem->x + 8, gem->y + 8};
    lv_obj_invalidate_area(s_board, &area);
}
static void take_board_frame(board_frame_t *frame) {
    memset(frame, 0, sizeof(*frame));
    frame->aim_x = px(120 + cosf(s_game.aim_radians) * 62);
    frame->aim_y = px(264 + sinf(s_game.aim_radians) * 62);
    frame->current_color = s_game.current_color;
    frame->next_color = s_game.next_color;
    for (int i = 0; i < s_game.count; i++) {
        float d = s_game.ball_distance[i];
        if (d < 0 || d >= s_game.path_length) continue;
        float x, y;
        zuma_path_position(&s_game, d, &x, &y);
        frame->balls[i] = (gem_frame_t){.x = px(x), .y = px(y),
                                       .color = s_game.balls[i], .visible = true};
    }
    if (s_game.shot.active)
        frame->shot = (gem_frame_t){.x = px(s_game.shot.x), .y = px(s_game.shot.y),
                                    .color = s_game.shot.color, .visible = true};
}
static void invalidate_board_changes(bool full) {
    board_frame_t current;
    take_board_frame(&current);
    if (full || !s_previous_frame_valid) {
        lv_obj_invalidate(s_board);
    } else {
        lv_area_t chain = {240, 320, -1, -1};
        for (int i = 0; i < ZUMA_MAX_BALLS; i++) {
            if (memcmp(&current.balls[i], &s_previous_frame.balls[i], sizeof(gem_frame_t)) == 0)
                continue;
            const gem_frame_t *changed[] = {&current.balls[i], &s_previous_frame.balls[i]};
            for (int j = 0; j < 2; j++) {
                if (!changed[j]->visible) continue;
                int x = changed[j]->x, y = changed[j]->y;
                if (x - 8 < chain.x1) chain.x1 = x - 8;
                if (y - 8 < chain.y1) chain.y1 = y - 8;
                if (x + 8 > chain.x2) chain.x2 = x + 8;
                if (y + 8 > chain.y2) chain.y2 = y + 8;
            }
        }
        if (chain.x1 <= chain.x2 && chain.y1 <= chain.y2)
            lv_obj_invalidate_area(s_board, &chain);
        if (current.aim_x != s_previous_frame.aim_x ||
            current.aim_y != s_previous_frame.aim_y ||
            current.current_color != s_previous_frame.current_color ||
            current.next_color != s_previous_frame.next_color) {
            lv_area_t launcher = {50, 197, 188, 286};
            lv_obj_invalidate_area(s_board, &launcher);
        }
        if (memcmp(&current.shot, &s_previous_frame.shot, sizeof(gem_frame_t)) != 0) {
            invalidate_gem(&s_previous_frame.shot);
            invalidate_gem(&current.shot);
        }
    }
    s_previous_frame = current;
    s_previous_frame_valid = true;
}
static void board_draw(lv_event_t *ev) {
    lv_layer_t *layer = lv_event_get_layer(ev);
    if (!layer) return;
    const board_frame_t *frame = &s_previous_frame;
    line(layer, 120, 264, frame->aim_x, frame->aim_y, 2, 0x9ad9ca);
    draw_image(layer, &zuma_guardian_image, 84, 211);
    gem(layer, 120, 264, frame->current_color);
    gem(layer, 162, 272, frame->next_color);
    for (int i = ZUMA_MAX_BALLS-1; i >= 0; i--) {
        const gem_frame_t *ball = &frame->balls[i];
        if (ball->visible) gem(layer, ball->x, ball->y, ball->color);
    }
    if (frame->shot.visible)
        gem(layer, frame->shot.x, frame->shot.y, frame->shot.color);
}
static lv_obj_t *label(lv_obj_t *parent, int x, int y, int w, int h,
                       const char *text, uint32_t color, const lv_font_t *font,
                       lv_text_align_t align) {
    lv_obj_t *o = lv_label_create(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, align, 0);
    lv_label_set_text(o, text);
    return o;
}
static void build_ui(void) {
    lv_display_t *display = lv_display_get_default();
    lv_display_add_event_cb(display, render_event, LV_EVENT_RENDER_READY, NULL);
    lv_display_add_event_cb(display, flush_event, LV_EVENT_FLUSH_START, NULL);
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x071517), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_t *background = lv_image_create(s_screen);
    lv_image_set_src(background, &zuma_board_image);
    lv_obj_set_pos(background, 0, 0);
    s_board = lv_obj_create(s_screen);
    lv_obj_set_size(s_board, 240, 320);
    lv_obj_set_pos(s_board, 0, 0);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_board, 0, 0);
    lv_obj_set_style_pad_all(s_board, 0, 0);
    lv_obj_add_event_cb(s_board, board_draw, LV_EVENT_DRAW_MAIN, NULL);
    (void)label(s_screen, 12, 10, 170, 29, "星环弹珠", 0xf5dfb2,
                &zuma_cjk_20, LV_TEXT_ALIGN_LEFT);
    s_battery = label(s_screen, 181, 13, 47, 20, "--", 0xc7dfd9,
                      &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT);
    s_subtitle = label(s_screen, 8, 302, 224, 18, "", 0xbed0b7,
                       &zuma_cjk_14, LV_TEXT_ALIGN_CENTER);
    s_score = label(s_screen, 18, 286, 120, 19, "", 0xf5dfb2,
                    &zuma_cjk_14, LV_TEXT_ALIGN_LEFT);
    s_progress = label(s_screen, 145, 286, 80, 19, "", 0xc7dfd9,
                       &zuma_cjk_14, LV_TEXT_ALIGN_RIGHT);
    s_prompt = label(s_screen, 24, 99, 192, 72, "", 0xffffff,
                     &zuma_cjk_16, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_bg_color(s_prompt, lv_color_hex(0x071b1b), 0);
    lv_obj_set_style_bg_opa(s_prompt, LV_OPA_90, 0);
    lv_obj_set_style_border_color(s_prompt, lv_color_hex(0xc99b51), 0);
    lv_obj_set_style_border_width(s_prompt, 1, 0);
    lv_obj_set_style_radius(s_prompt, 5, 0);
    lv_obj_set_style_pad_top(s_prompt, 12, 0);
    lv_screen_load(s_screen);
}
static void refresh_ui(void) {
    static const char *levels[] = {"悠闲", "经典", "风暴"};
    static zuma_mode_t shown_mode = (zuma_mode_t)-1;
    static uint8_t shown_difficulty = 255;
    static uint32_t shown_score = UINT32_MAX;
    static uint8_t shown_spawned = 255;
    static int shown_hint = -1;
    int hint = s_game.mode == ZUMA_PLAYING ? (int)((esp_timer_get_time() / 4000000) & 1) : -1;
    if (shown_mode == s_game.mode && shown_difficulty == s_game.difficulty &&
        shown_score == s_game.score && shown_spawned == s_game.spawned &&
        shown_hint == hint) {
        if (s_game.mode == ZUMA_PLAYING) invalidate_board_changes(false);
        return;
    }
    bool mode_changed = shown_mode != s_game.mode;
    shown_mode = s_game.mode;
    shown_difficulty = s_game.difficulty;
    shown_score = s_game.score;
    shown_spawned = s_game.spawned;
    shown_hint = hint;
    switch (s_game.mode) {
    case ZUMA_MENU:
        lv_obj_remove_flag(s_prompt, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_subtitle, "上/下选难度  确定开始");
        lv_label_set_text_fmt(s_prompt, "< %s >\n确定开始", levels[s_game.difficulty]);
        break;
    case ZUMA_PLAYING:
        lv_obj_add_flag(s_prompt, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_subtitle, hint ? "双击换珠  长按暂停" :
                          "上/下瞄准  确定发射");
        break;
    case ZUMA_PAUSED:
        lv_obj_remove_flag(s_prompt, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_subtitle, "长按确定返回标题");
        lv_label_set_text(s_prompt, "已暂停\n确定继续");
        break;
    case ZUMA_WON:
        lv_obj_remove_flag(s_prompt, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_subtitle, "长按确定返回标题");
        lv_label_set_text(s_prompt, "星环守住了\n确定再玩");
        break;
    case ZUMA_LOST:
        lv_obj_remove_flag(s_prompt, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_subtitle, "长按确定返回标题");
        lv_label_set_text(s_prompt, "日蚀降临\n确定再玩");
        break;
    }
    lv_label_set_text_fmt(s_score, "分 %06lu", (unsigned long)s_game.score);
    lv_label_set_text_fmt(s_progress, "波 %02u/%02u", s_game.spawned, s_game.quota);
    invalidate_board_changes(mode_changed);
}
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE,
                          esp_timer_get_time())) return;
    if (!s_input_ready || !s_keys) return;
    key_message_t m = {button, event};
    (void)xQueueSend(s_keys, &m, 0);
}
static void handle_key(const key_message_t *m) {
    if (m->button == BSP_BTN_UP || m->button == BSP_BTN_DOWN) {
        int direction = m->button == BSP_BTN_UP ? -1 : 1;
        if (m->event == BSP_BTN_PRESS) {
            s_held_dir = direction;
            s_repeat_clock = 0;
            zuma_turn(&s_game, direction);
        } else if (m->event == BSP_BTN_RELEASE && s_held_dir == direction) {
            s_held_dir = 0;
        }
        return;
    }
    if (m->button != BSP_BTN_OK) return;
    s_held_dir = 0;
    if (m->event == BSP_BTN_CLICK) {
        if (s_game.mode == ZUMA_PAUSED) zuma_toggle_pause(&s_game);
        else zuma_fire(&s_game);
    }
    else if (m->event == BSP_BTN_DOUBLE) zuma_swap(&s_game);
    else if (m->event == BSP_BTN_LONG) {
        if (s_game.mode == ZUMA_PLAYING) zuma_toggle_pause(&s_game);
        else zuma_to_menu(&s_game);
    }
}
static void battery_task(void *arg) {
    (void)arg;
    for (;;) {
        s_battery_soc = bsp_battery_soc();
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
static void game_task(void *arg) {
    (void)arg;
    TickType_t previous = xTaskGetTickCount();
    int64_t last_us = esp_timer_get_time();
    int64_t perf_start_us = last_us;
    uint32_t perf_updates = 0;
    uint32_t perf_last_rendered = 0;
    uint32_t perf_last_pixels = 0;
    int battery_ticks = 0;
    int shown_soc = -2;
    for (;;) {
        vTaskDelayUntil(&previous, pdMS_TO_TICKS(33));
        int64_t now = esp_timer_get_time();
        float dt = (now - last_us) / 1000000.0f;
        last_us = now;
        if (!bsp_lvgl_lock(100)) continue;
        key_message_t m;
        while (xQueueReceive(s_keys, &m, 0) == pdTRUE) handle_key(&m);
        if (s_held_dir && s_game.mode == ZUMA_PLAYING) {
            s_repeat_clock += dt;
            if (s_repeat_clock > 0.055f) {
                zuma_turn(&s_game, s_held_dir);
                s_repeat_clock = 0;
            }
        }
        zuma_tick(&s_game, dt);
        refresh_ui();
        if (++battery_ticks >= 30) {
            battery_ticks = 0;
            int soc = s_battery_soc;
            if (soc != shown_soc) {
                shown_soc = soc;
                if (soc >= 0) lv_label_set_text_fmt(s_battery, "%d%%", soc);
                else lv_label_set_text(s_battery, "--");
            }
        }
        zuma_sound_t sound = zuma_take_sound(&s_game);
        bool playing = s_game.mode == ZUMA_PLAYING;
        uint32_t rendered = s_rendered_frames;
        uint32_t pixels = s_flushed_pixels;
        bsp_lvgl_unlock();
        if (s_audio_ok && sound != ZUMA_SOUND_NONE) zuma_audio_cue(sound);
        if (playing) {
            perf_updates++;
            int64_t elapsed = now - perf_start_us;
            if (elapsed >= 5000000) {
                uint32_t render_count = rendered - perf_last_rendered;
                uint32_t pixel_count = pixels - perf_last_pixels;
                ESP_LOGI(TAG, "5s performance: game=%lu.%01lu fps, LVGL=%lu.%01lu fps, SPI=%lu px",
                         (unsigned long)(perf_updates * 1000000ULL / elapsed),
                         (unsigned long)((perf_updates * 10000000ULL / elapsed) % 10),
                         (unsigned long)(render_count * 1000000ULL / elapsed),
                         (unsigned long)((render_count * 10000000ULL / elapsed) % 10),
                         (unsigned long)pixel_count);
                perf_start_us = now;
                perf_updates = 0;
                perf_last_rendered = rendered;
                perf_last_pixels = pixels;
            }
        } else {
            perf_start_us = now;
            perf_updates = 0;
            perf_last_rendered = rendered;
            perf_last_pixels = pixels;
        }
    }
}
void app_main(void) {
    if (esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_GPIO))
        key_guard_arm(&s_key_guard, esp_timer_get_time(), 10000000LL);
    zuma_init(&s_game, (uint32_t)esp_timer_get_time());
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display startup failed");
        return;
    }
    s_keys = xQueueCreate(16, sizeof(key_message_t));
    if (!s_keys) { ESP_LOGE(TAG, "Input queue allocation failed"); return; }
    if (bsp_lvgl_lock(1000)) {
        build_ui();
        refresh_ui();
        bsp_lvgl_unlock();
    } else { ESP_LOGE(TAG, "LVGL lock failed"); return; }
    bsp_display_backlight(85);
    if (bsp_button_init(on_key, NULL) != ESP_OK)
        ESP_LOGE(TAG, "Buttons unavailable");
    else if (s_key_guard.armed)
        key_guard_sync_released(&s_key_guard,
                                bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    if (bsp_battery_init() != ESP_OK)
        ESP_LOGW(TAG, "Battery gauge unavailable");
    else if (xTaskCreate(battery_task, "orbit_battery", 2048, NULL, 2, NULL) != pdPASS)
        ESP_LOGW(TAG, "Battery worker allocation failed");
    s_audio_ok = zuma_audio_start();
    if (!s_audio_ok) ESP_LOGW(TAG, "Audio unavailable; continuing silently");
    if (xTaskCreate(game_task, "star_orbit", 4096, NULL, 4, NULL) != pdPASS)
        ESP_LOGE(TAG, "Game task allocation failed");
    else s_input_ready = true;
}
