/* GRAVITY SIEGE: standalone Passport physics arcade (no demo menu).
 * Runs fixed-step simulation in the LVGL timer task. Hardware button callbacks
 * enqueue small events only, avoiding display access in the esp_timer context. */
#include "kinetic_core.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include <math.h>
#include <stdint.h>

#define COLOR_BG       0x0B1425u
#define COLOR_TOP      0x142D41u
#define COLOR_PANEL    0x152437u
#define COLOR_FLOOR    0x4F6377u
#define COLOR_GOLD     0xFFC867u
#define COLOR_GLASS    0x55E2EEu
#define COLOR_STONE    0x8D95A9u
#define COLOR_BALL     0xFF8152u
#define COLOR_TEXT     0xF3F5F5u
#define INPUT_QUEUE_SIZE 12
#define AIM_DOTS 7
#define SIM_STEP_US 16667ull

typedef struct {
    bsp_btn_t key;
    bsp_btn_ev_t event;
} game_event_t;

static const char *TAG = "gravity_siege";
static kin_game_t s_game;
static QueueHandle_t s_events;
static lv_obj_t *s_root, *s_bodies[KIN_MAX_BODIES];
static lv_obj_t *s_marks[KIN_MAX_BODIES];
static lv_obj_t *s_aim[AIM_DOTS];
static lv_obj_t *s_flash, *s_status;
static lv_obj_t *s_title, *s_score, *s_battery, *s_power, *s_help;
static uint8_t s_style[KIN_MAX_BODIES];
static uint32_t s_frames;
static uint64_t s_last_us, s_lag_us;
static bool s_battery_ok;

static lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h,
                      uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y,
                       const char *text, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    return obj;
}

static void create_game_ui(void)
{
    s_root = lv_obj_create(NULL);
    lv_obj_set_size(s_root, 240, 320);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_root, 0, 0);
    lv_obj_set_style_radius(s_root, 0, 0);
    lv_obj_set_style_pad_all(s_root, 0, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    (void)rect(s_root, 0, 0, 240, 47, COLOR_TOP, 0);
    (void)rect(s_root, 0, 47, 240, 2, COLOR_GOLD, 0);
    s_title = label(s_root, 9, 5, "GRAVITY SIEGE", COLOR_GOLD);
    s_score = label(s_root, 9, 26, "SCORE 000000", COLOR_TEXT);
    s_battery = label(s_root, 179, 5, "", COLOR_TEXT);

    /* Industrial test arena: static backdrop and floor are independent of
     * the original Passport BSP demo menu and pixel UI shell. */
    for (int i = 0; i < 7; ++i) {
        (void)rect(s_root, 14 + i * 35, 66, 1, 190, 0x1D2B3E, 0);
    }
    for (int i = 0; i < 6; ++i) {
        (void)rect(s_root, 14, 77 + i * 32, 211, 1, 0x1A293B, 0);
    }
    (void)rect(s_root, 6, 271, 228, 4, COLOR_FLOOR, 0);
    for (int i = 0; i < 15; ++i) {
        (void)rect(s_root, 8 + 16*i, 276, 7, 2, 0x43586A, 0);
    }

    /* The reusable pool avoids LVGL allocations while physics is running. */
    for (int i = 0; i < KIN_MAX_BODIES; ++i) {
        s_bodies[i] = rect(s_root, 0, 0, 16, 16, COLOR_GLASS, LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(s_bodies[i], 2, 0);
        lv_obj_set_style_border_color(s_bodies[i], lv_color_hex(0xE8FFFF), 0);
        s_marks[i] = rect(s_root, 0, 0, 3, 3, COLOR_TEXT, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(s_bodies[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
        s_style[i] = KIN_NONE;
    }
    for (int i = 0; i < AIM_DOTS; ++i) {
        s_aim[i] = rect(s_root, 0, 0, 3, 3, COLOR_GOLD, LV_RADIUS_CIRCLE);
    }
    s_flash = rect(s_root, 0, 0, 16, 16, COLOR_GOLD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_bg_opa(s_flash, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_flash, 2, 0);
    lv_obj_set_style_border_color(s_flash, lv_color_hex(COLOR_GOLD), 0);
    lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);

    (void)rect(s_root, 0, 282, 240, 38, COLOR_PANEL, 0);
    (void)rect(s_root, 0, 282, 240, 1, COLOR_GOLD, 0);
    s_power = label(s_root, 9, 283, "ANGLE 00  PWR 2", COLOR_GOLD);
    s_help = label(s_root, 9, 302, "UP/DN AIM  OK FIRE", COLOR_TEXT);
    s_status = label(s_root, 15, 137, "", COLOR_TEXT);
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    lv_screen_load(s_root);
}

static void update_ball_style(int i, uint8_t kind)
{
    if (s_style[i] == kind) return;
    s_style[i] = kind;
    uint32_t bg = kind == KIN_PROJECTILE ? COLOR_BALL :
                  kind == KIN_STONE ? COLOR_STONE : COLOR_GLASS;
    uint32_t edge = kind == KIN_PROJECTILE ? 0xFFEEC8 :
                    kind == KIN_STONE ? 0xE5E6F4 : 0xD3FEFF;
    lv_obj_set_style_bg_color(s_bodies[i], lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(s_bodies[i], lv_color_hex(edge), 0);
    lv_obj_set_style_bg_color(s_marks[i], lv_color_hex(edge), 0);
}

static void draw_scene(void)
{
    for (int i = 0; i < KIN_MAX_BODIES; ++i) {
        const kin_body_t *b = &s_game.bodies[i];
        if (!b->alive) {
            lv_obj_add_flag(s_bodies[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        update_ball_style(i, b->kind);
        int d = (int)(2.0f * b->radius + 0.5f);
        lv_obj_set_size(s_bodies[i], d, d);
        lv_obj_set_pos(s_bodies[i], (int)(b->x - b->radius),
                      (int)(b->y - b->radius));
        lv_obj_remove_flag(s_bodies[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_marks[i],
                       (int)(b->x + cosf(b->angle) * b->radius * 0.52f - 1.5f),
                       (int)(b->y + sinf(b->angle) * b->radius * 0.52f - 1.5f));
        lv_obj_remove_flag(s_marks[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (s_game.phase == KIN_PLAY && !s_game.paused) {
        float rad = (float)s_game.aim_deg * 0.01745329252f;
        for (int i = 0; i < AIM_DOTS; ++i) {
            float t = (float)(i + 1) * 6.0f;
            lv_obj_set_pos(s_aim[i],
                (int)(25.0f + cosf(rad) * t),
                (int)(243.0f - sinf(rad) * t));
            lv_obj_remove_flag(s_aim[i], LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        for (int i = 0; i < AIM_DOTS; ++i)
            lv_obj_add_flag(s_aim[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (s_game.flash) {
        int radius = 9 + (12 - s_game.flash) * 3;
        lv_obj_set_pos(s_flash, (int)s_game.flash_x - radius,
                       (int)s_game.flash_y - radius);
        lv_obj_set_size(s_flash, radius * 2, radius * 2);
        lv_obj_remove_flag(s_flash, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_flash, LV_OBJ_FLAG_HIDDEN);

    if (s_game.paused) {
        lv_label_set_text(s_status, "       PAUSED");
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    } else if (s_game.phase == KIN_WON) {
        lv_label_set_text(s_status, "CLEAR!  OK NEXT");
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    } else if (s_game.phase == KIN_LOST) {
        lv_label_set_text(s_status, "GAME OVER  OK RETRY");
        lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);

    if ((++s_frames % 5u) == 0u) {
        lv_label_set_text_fmt(s_score, "S%02u %06lu  T%02u",
            (unsigned)s_game.stage, (unsigned long)s_game.score,
            (unsigned)s_game.targets);
        lv_label_set_text_fmt(s_power, "AIM %02u   PWR %u   SHOTS %u",
            (unsigned)s_game.aim_deg, (unsigned)(s_game.power + 1),
            (unsigned)s_game.shots);
        if (s_battery_ok && (s_frames % 150u) == 0u) {
            int soc = bsp_battery_soc();
            if (soc >= 0 && soc <= 100)
                lv_label_set_text_fmt(s_battery, "%d%%", soc);
            else lv_label_set_text(s_battery, "");
        }
    }
}

static void handle_input(game_event_t event)
{
    if (event.event == BSP_BTN_LONG) {
        if (event.key == BSP_BTN_UP) kin_power_next(&s_game);
        if (event.key == BSP_BTN_DOWN) kin_new_game(&s_game, esp_random());
        if (event.key == BSP_BTN_OK && s_game.phase == KIN_PLAY)
            s_game.paused = !s_game.paused;
        return;
    }
    if (event.event != BSP_BTN_CLICK) return;
    if (event.key == BSP_BTN_UP) kin_aim(&s_game, 5);
    else if (event.key == BSP_BTN_DOWN) kin_aim(&s_game, -5);
    else if (event.key == BSP_BTN_OK) {
        if (s_game.phase == KIN_WON) kin_next_stage(&s_game);
        else if (s_game.phase == KIN_LOST)
            kin_new_game(&s_game, esp_random());
        else if (s_game.paused) s_game.paused = 0;
        else (void)kin_fire(&s_game);
    }
}

static void on_button(bsp_btn_t btn, bsp_btn_ev_t ev, void *ctx)
{
    (void)ctx;
    if (!s_events || (ev != BSP_BTN_CLICK && ev != BSP_BTN_LONG)) return;
    const game_event_t item = { .key = btn, .event = ev };
    (void)xQueueSend(s_events, &item, 0);
}

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    game_event_t ev;
    int read = 0;
    while (read++ < INPUT_QUEUE_SIZE &&
           xQueueReceive(s_events, &ev, 0) == pdTRUE)
        handle_input(ev);

    uint64_t now = (uint64_t)esp_timer_get_time();
    uint64_t delta = now - s_last_us;
    s_last_us = now;
    if (delta > 100000ull) delta = 100000ull;
    if (s_game.paused) s_lag_us = 0;
    else s_lag_us += delta;
    int steps = 0;
    while (s_lag_us >= SIM_STEP_US && steps < 5) {
        kin_step(&s_game);
        s_lag_us -= SIM_STEP_US;
        ++steps;
    }
    /* A slow screen must not cause an unbounded spiral of catch-up work. */
    if (s_lag_us > 4 * SIM_STEP_US) s_lag_us = 4 * SIM_STEP_US;
    draw_scene();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Gravity Siege standalone arcade boot");
    (void)bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display unavailable (SPI pins %d/%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK);
        return;
    }
    bsp_display_backlight(88);
    s_battery_ok = bsp_battery_init() == ESP_OK;
    s_events = xQueueCreate(INPUT_QUEUE_SIZE, sizeof(game_event_t));
    if (!s_events) {
        ESP_LOGE(TAG, "Input event queue allocation failed");
        return;
    }
    kin_new_game(&s_game, esp_random());
    if (bsp_button_init(on_button, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Buttons unavailable, game remains view-only");
    }
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL lock unavailable");
        return;
    }
    create_game_ui();
    s_last_us = (uint64_t)esp_timer_get_time();
    (void)lv_timer_create(frame_cb, 33, NULL); /* 30 FPS visual target */
    draw_scene();
    bsp_lvgl_unlock();
}
