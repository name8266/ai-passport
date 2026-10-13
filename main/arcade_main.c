#include "arcade_art.h"
#include "arcade_game.h"
#include "arcade_docking.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "key_guard.h"
#include "lvgl.h"
#include "zuma_audio.h"
#include <math.h>
#include <string.h>

LV_FONT_DECLARE(arcade_nine_14);
LV_FONT_DECLARE(arcade_nine_20);

static const char *TAG = "classic_nine";
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } key_message_t;
typedef struct { int16_t x, y, w, h; uint8_t color, style; } shape_t;
typedef struct { shape_t shapes[192]; uint16_t count; arcade_kind_t kind; } scene_t;

static arcade_game_t s_game;
static QueueHandle_t s_keys;
static lv_obj_t *s_screen, *s_background, *s_board, *s_title, *s_battery;
static lv_obj_t *s_hud, *s_footer, *s_panel, *s_panel_title, *s_panel_body;
static scene_t s_scene;
static bool s_scene_valid, s_demo, s_audio_ok;
static volatile bool s_input_ready;
static volatile int s_battery_soc = -1;
static volatile uint32_t s_rendered, s_pixels;
static key_guard_t s_key_guard;
static float s_idle_time, s_demo_time;
static int s_demo_index;

static const uint32_t PALETTE[] = {
    0xFFE4A1, 0xF57776, 0x81E4C7, 0x87C8FF,
    0xC7A4FF, 0xD7F185, 0x233B49, 0xFFFFFF
};
static int round_px(float value) { return (int)lroundf(value); }
static void put(scene_t *scene, int x, int y, int w, int h, int color, int style) {
    if (scene->count >= 192) return;
    scene->shapes[scene->count++] = (shape_t){
        .x = x, .y = y, .w = w, .h = h, .color = color, .style = style
    };
}
static void collect_scene(scene_t *scene) {
    memset(scene, 0, sizeof(*scene));
    scene->kind = s_game.kind;
    if (s_game.mode == ARCADE_HOME) return;
    switch (s_game.kind) {
    case ARCADE_BREAKOUT:
        for (int r = 0; r < 5; r++) for (int c = 0; c < 6; c++)
            put(scene, 12 + c * 36, 55 + r * 18, s_game.bricks[r][c] ? 32 : 0,
                13, (r + c) % 5, 1);
        put(scene, round_px(s_game.angle) - 30, 246, 60, 18, 0, 9);
        put(scene, round_px(s_game.x) - 6, round_px(s_game.y) - 6, 12, 12, 7, 0);
        break;
    case ARCADE_TETRIS:
        for (int y = 0; y < 18; y++) for (int x = 0; x < 10; x++)
            put(scene, 40 + x * 16, 60 + y * 12, s_game.grid[y][x] ? 15 : 0,
                11, 1, 9);
        for (int py = 0; py < 4; py++) for (int px = 0; px < 4; px++)
            if (arcade_tetris_cell(s_game.piece, s_game.rotation, px, py) &&
                s_game.piece_y + py >= 0)
                put(scene, 40 + (s_game.piece_x + px) * 16,
                    60 + (s_game.piece_y + py) * 12, 15, 11, 1, 9);
        break;
    case ARCADE_DOODLE:
        for (int i = 0; i < 7; i++)
            put(scene, round_px(s_game.actors[i].x) - 25,
                round_px(s_game.actors[i].y), 50, 8, i % 2 ? 2 : 0, 3);
        put(scene, round_px(s_game.x) - 11, round_px(s_game.y) - 13, 22, 26, 2, 9);
        break;
    case ARCADE_SNAKE:
        put(scene, 24 + s_game.food_x * 16 + 2,
            48 + s_game.food_y * 16 + 2, 12, 12, 1, 0);
        for (int i = 0; i < s_game.snake_length; i++)
            put(scene, 24 + s_game.snake_x[i] * 16 + (i ? 1 : -1),
                48 + s_game.snake_y[i] * 16 + (i ? 1 : -1),
                i ? 14 : 18, i ? 14 : 18, i ? 2 : 3, i ? 0 : 9);
        break;
    case ARCADE_SHOOTER:
        for (int i = 0; i < 6; i++)
            put(scene, round_px(s_game.actors[i].x) - 10,
                round_px(s_game.actors[i].y) - 9, 20, 18, 1 + i % 4, 4);
        for (int i = 0; i < 4; i++)
            put(scene, round_px(s_game.shots[i].x) - 2,
                round_px(s_game.shots[i].y) - 8,
                s_game.shots[i].active ? 4 : 0, 13, 0, 7);
        put(scene, round_px(s_game.x) - 15, 250, 30, 29, 4, 9);
        break;
    case ARCADE_FLAPPY:
        for (int i = 0; i < 3; i++) {
            int x = round_px(s_game.actors[i].x), gap = round_px(s_game.actors[i].y);
            put(scene, x, 30, 28, gap - 67, 6, 5);
            put(scene, x, gap + 38, 28, 286 - gap - 38, 6, 5);
        }
        put(scene, round_px(s_game.x) - 12, round_px(s_game.y) - 10, 24, 20, 5, 9);
        break;
    case ARCADE_LANDER:
        put(scene, 77, 267, 86, 6, 2, 8);
        put(scene, round_px(s_game.x) - 12, round_px(s_game.y) - 12, 24, 24, 6, 9);
        put(scene, round_px(s_game.x) - 4, round_px(s_game.y) + 10,
            s_game.ok_held && s_game.fuel > 0 ? 8 : 0, 15, 0, 7);
        break;
    case ARCADE_DOCKING: {
        float depth = s_game.dock_z + 10.0f;
        int cx = 120 - round_px(s_game.dock_x * 320.0f / depth);
        int cy = 161 + round_px(s_game.dock_y * 320.0f / depth);
        int radius = 24 + round_px(580.0f / depth);
        if (radius > 150) radius = 150;
        put(scene, cx - radius, cy - radius, radius * 2, radius * 2, 0, 10);
        put(scene, cx - radius * 3 / 4, cy - radius * 3 / 4,
            radius * 3 / 2, radius * 3 / 2, 3, 10);
        put(scene, cx - 24, cy - 24, 48, 48, ARCADE_DOCKING, 9);
        put(scene, cx - 3, cy - 3, 6, 6, 0, 0);
        int aim_x = 120 + round_px(s_game.dock_yaw * 110.0f);
        int aim_y = 161 - round_px(s_game.dock_pitch * 110.0f);
        put(scene, aim_x - 16, aim_y, 12, 2, 2, 1);
        put(scene, aim_x + 4, aim_y, 12, 2, 2, 1);
        put(scene, aim_x, aim_y - 16, 2, 12, 2, 1);
        put(scene, aim_x, aim_y + 4, 2, 12, 2, 1);
        put(scene, 108 - round_px(s_game.dock_roll * 40.0f),
            260, 24, 20, ARCADE_DOCKING, 11);
        break;
    }
    case ARCADE_RACER:
        for (int i = 0; i < 6; i++)
            put(scene, round_px(s_game.actors[i].x) - 12,
                round_px(s_game.actors[i].y) - 17,
                s_game.actors[i].active ? 24 : 0, 34, 1 + i % 4, 6);
        put(scene, round_px(s_game.x) - 14, 240, 28, 36, 7, 9);
        break;
    default: break;
    }
}
static bool shape_visible(const shape_t *s) {
    return s->w > 0 && s->h > 0 && s->x < 240 && s->y < 320 &&
           s->x + s->w > 0 && s->y + s->h > 0;
}
static void invalidate_shape(const shape_t *s) {
    if (!shape_visible(s)) return;
    lv_area_t area = {s->x - 2, s->y - 2, s->x + s->w + 2, s->y + s->h + 2};
    lv_obj_invalidate_area(s_board, &area);
}
static void invalidate_change(const shape_t *old, const shape_t *next) {
    if (!shape_visible(old) || !shape_visible(next)) {
        invalidate_shape(old); invalidate_shape(next); return;
    }
    int left = old->x < next->x ? old->x : next->x;
    int top = old->y < next->y ? old->y : next->y;
    int right = old->x + old->w > next->x + next->w ?
                old->x + old->w : next->x + next->w;
    int bottom = old->y + old->h > next->y + next->h ?
                 old->y + old->h : next->y + next->h;
    int union_area = (right - left) * (bottom - top);
    int separate_area = old->w * old->h + next->w * next->h;
    if (union_area <= separate_area + 128) {
        lv_area_t area = {left - 2, top - 2, right + 2, bottom + 2};
        lv_obj_invalidate_area(s_board, &area);
    } else {
        invalidate_shape(old); invalidate_shape(next);
    }
}
static void update_scene(bool full) {
    scene_t current;
    collect_scene(&current);
    if (full || !s_scene_valid || current.kind != s_scene.kind) {
        lv_obj_invalidate(s_board);
    } else {
        int count = current.count > s_scene.count ? current.count : s_scene.count;
        for (int i = 0; i < count; i++) {
            shape_t empty = {0};
            const shape_t *old = i < s_scene.count ? &s_scene.shapes[i] : &empty;
            const shape_t *new = i < current.count ? &current.shapes[i] : &empty;
            if (memcmp(old, new, sizeof(*old)) != 0) invalidate_change(old, new);
        }
    }
    s_scene = current;
    s_scene_valid = true;
}
static void board_draw(lv_event_t *event) {
    lv_layer_t *layer = lv_event_get_layer(event);
    if (!layer) return;
    const lv_area_t *clip = &layer->_clip_area;
    for (int i = 0; i < s_scene.count; i++) {
        const shape_t *s = &s_scene.shapes[i];
        if (s->w <= 0 || s->h <= 0 || s->x > clip->x2 || s->y > clip->y2 ||
            s->x + s->w < clip->x1 || s->y + s->h < clip->y1) continue;
        lv_area_t area = {s->x, s->y, s->x + s->w - 1, s->y + s->h - 1};
        if (s->style == 10) {
            lv_draw_rect_dsc_t ring;
            lv_draw_rect_dsc_init(&ring);
            ring.bg_opa = LV_OPA_TRANSP;
            ring.radius = LV_RADIUS_CIRCLE;
            ring.border_width = 2;
            ring.border_opa = LV_OPA_COVER;
            ring.border_color = lv_color_hex(PALETTE[s->color % 8]);
            lv_draw_rect(layer, &ring, &area);
            continue;
        }
        if (s->style == 11) {
            lv_draw_image_dsc_t ship;
            lv_draw_image_dsc_init(&ship);
            ship.src = &arcade_docking_ship;
            lv_draw_image(layer, &ship, &area);
            continue;
        }
        if (s->style == 9) {
            if (s->color != ARCADE_TETRIS) {
                lv_draw_rect_dsc_t backing;
                lv_draw_rect_dsc_init(&backing);
                backing.bg_color = lv_color_hex(0x07121D);
                backing.bg_opa = LV_OPA_40;
                backing.radius = LV_RADIUS_CIRCLE;
                lv_draw_rect(layer, &backing, &area);
            }
            lv_draw_image_dsc_t image;
            lv_draw_image_dsc_init(&image);
            image.src = &arcade_sprites[s->color];
            lv_draw_image(layer, &image, &area);
            continue;
        }
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = lv_color_hex(PALETTE[s->color % 8]);
        d.bg_opa = LV_OPA_COVER;
        d.radius = s->style == 0 ? LV_RADIUS_CIRCLE :
                   (s->style == 1 || s->style == 5 ? 2 : 5);
        d.border_width = 1;
        d.border_color = lv_color_hex(s->style == 5 ? 0xFFE4A1 : 0xFFFFFF);
        d.border_opa = s->style == 5 ? LV_OPA_60 : LV_OPA_90;
        lv_draw_rect(layer, &d, &area);
        if (s->w >= 12 && s->h >= 10 && s->style != 5) {
            lv_area_t glint = {s->x + 3, s->y + 2, s->x + s->w / 2,
                               s->y + 3};
            d.border_width = 0;
            d.bg_color = lv_color_hex(0xFFFFFF);
            d.bg_opa = LV_OPA_70;
            d.radius = 1;
            lv_draw_rect(layer, &d, &glint);
        }
    }
}
static lv_obj_t *text_label(int x, int y, int width, int height,
                            const char *text, const lv_font_t *font,
                            lv_text_align_t align, uint32_t color) {
    lv_obj_t *label = lv_label_create(s_screen);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text);
    return label;
}
static void render_event(lv_event_t *event) {
    (void)event;
    s_rendered++;
}
static void flush_event(lv_event_t *event) {
    const lv_area_t *area = lv_event_get_param(event);
    if (area) s_pixels += (uint32_t)lv_area_get_width(area) * lv_area_get_height(area);
}
static void build_ui(void) {
    lv_display_t *display = lv_display_get_default();
    lv_display_add_event_cb(display, render_event, LV_EVENT_RENDER_READY, NULL);
    lv_display_add_event_cb(display, flush_event, LV_EVENT_FLUSH_START, NULL);
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x08121E), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    s_background = lv_image_create(s_screen);
    lv_image_set_src(s_background, &arcade_backgrounds[0]);
    lv_obj_set_pos(s_background, 0, 0);
    s_board = lv_obj_create(s_screen);
    lv_obj_set_pos(s_board, 0, 0);
    lv_obj_set_size(s_board, 240, 320);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_board, 0, 0);
    lv_obj_set_style_pad_all(s_board, 0, 0);
    lv_obj_add_event_cb(s_board, board_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_title = text_label(12, 8, 174, 26, "经典游戏合集-9款", &arcade_nine_20,
                         LV_TEXT_ALIGN_LEFT, 0xFFE4A1);
    s_battery = text_label(184, 12, 44, 18, "--", &lv_font_montserrat_12,
                           LV_TEXT_ALIGN_RIGHT, 0xFFFFFF);
    s_hud = text_label(12, 35, 216, 20, "", &arcade_nine_14,
                       LV_TEXT_ALIGN_CENTER, 0xFFFFFF);
    s_footer = text_label(8, 296, 224, 22, "", &arcade_nine_14,
                          LV_TEXT_ALIGN_CENTER, 0xFFFFFF);
    lv_obj_t *readable[] = {s_title, s_battery, s_hud, s_footer};
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_bg_color(readable[i], lv_color_hex(0x08121E), 0);
        lv_obj_set_style_bg_opa(readable[i], LV_OPA_70, 0);
        lv_obj_set_style_radius(readable[i], 4, 0);
    }
    s_panel = lv_obj_create(s_screen);
    lv_obj_set_pos(s_panel, 18, 98);
    lv_obj_set_size(s_panel, 204, 120);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x091524), 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_80, 0);
    lv_obj_set_style_border_color(s_panel, lv_color_hex(0xDFC17B), 0);
    lv_obj_set_style_border_width(s_panel, 2, 0);
    lv_obj_set_style_radius(s_panel, 12, 0);
    lv_obj_set_style_pad_all(s_panel, 0, 0);
    lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    s_panel_title = lv_label_create(s_panel);
    lv_obj_set_pos(s_panel_title, 8, 16);
    lv_obj_set_size(s_panel_title, 184, 29);
    lv_obj_set_style_text_font(s_panel_title, &arcade_nine_20, 0);
    lv_obj_set_style_text_color(s_panel_title, lv_color_hex(0xFFE4A1), 0);
    lv_obj_set_style_text_align(s_panel_title, LV_TEXT_ALIGN_CENTER, 0);
    s_panel_body = lv_label_create(s_panel);
    lv_obj_set_pos(s_panel_body, 8, 54);
    lv_obj_set_size(s_panel_body, 184, 60);
    lv_obj_set_style_text_font(s_panel_body, &arcade_nine_14, 0);
    lv_obj_set_style_text_color(s_panel_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(s_panel_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_screen_load(s_screen);
}
static void refresh_ui(bool force) {
    static arcade_kind_t last_kind = ARCADE_GAME_COUNT;
    static arcade_mode_t last_mode = ARCADE_HOME;
    static uint32_t last_score = UINT32_MAX;
    static int last_lives = -1;
    static bool last_demo;
    static int last_hint_phase = -1;
    int hint_phase = (int)((esp_timer_get_time() / 6000000LL) & 1);
    bool page_changed = force || last_kind != s_game.kind || last_mode != s_game.mode ||
                        last_demo != s_demo;
    if (last_kind != s_game.kind || force)
        lv_image_set_src(s_background, &arcade_backgrounds[s_game.kind]);
    if (page_changed) {
        lv_label_set_text(s_title, s_game.mode == ARCADE_HOME ? "经典游戏合集-9款" : arcade_name(s_game.kind));
        if (s_game.mode == ARCADE_PLAYING) {
            lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_panel_title, s_game.mode == ARCADE_HOME ?
                arcade_name(s_game.kind) : s_game.mode == ARCADE_WON ? "挑战成功" : "挑战结束");
            if (s_game.mode == ARCADE_HOME)
                lv_label_set_text_fmt(s_panel_body, "%d / 9\n%s\n确定开始",
                                      (int)s_game.kind + 1, arcade_goal(s_game.kind));
            else lv_label_set_text_fmt(s_panel_body, "分 %lu\n确定再试一次",
                                       (unsigned long)s_game.score);
            lv_label_set_text(s_hud, s_game.mode == ARCADE_HOME ?
                              "9款独立游戏  三键畅玩" : arcade_goal(s_game.kind));
        }
    }
    if (page_changed || hint_phase != last_hint_phase) {
        lv_label_set_text(s_footer, s_game.mode == ARCADE_HOME ?
            "上/下选游戏  确定开始" :
            s_demo ? "试玩演示  任意键返回" :
            s_game.mode == ARCADE_PLAYING ?
                (hint_phase ? (arcade_long_exit_uses_up(s_game.kind) ?
                    "长按上键返回合集" : "长按确定返回合集") : arcade_hint(s_game.kind)) :
                "确定重试  长按返回");
        last_hint_phase = hint_phase;
    }
    if (s_game.mode == ARCADE_PLAYING && s_game.kind == ARCADE_DOCKING) {
        static uint32_t hud_tick = 0;
        if (page_changed || (hud_tick++ % 3u) == 0)
            lv_label_set_text_fmt(s_hud, "%s %.1fm V %.2f",
                arcade_docking_axis_name(s_game.dock_axis),
                (double)s_game.dock_z, (double)s_game.dock_vz);
    } else if (page_changed || last_score != s_game.score || last_lives != s_game.lives) {
        if (s_game.mode == ARCADE_PLAYING)
            lv_label_set_text_fmt(s_hud, "分 %lu   生命 %d", (unsigned long)s_game.score, s_game.lives);
    }
    update_scene(page_changed);
    last_kind = s_game.kind; last_mode = s_game.mode;
    last_score = s_game.score; last_lives = s_game.lives; last_demo = s_demo;
}
static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *user) {
    (void)user;
    if (key_guard_consume(&s_key_guard, event == BSP_BTN_RELEASE,
                          esp_timer_get_time())) return;
    if (!s_input_ready || !s_keys) return;
    key_message_t message = {button, event};
    (void)xQueueSend(s_keys, &message, 0);
}
static void handle_key(key_message_t message) {
    if (s_demo) {
        if (message.event == BSP_BTN_PRESS || message.event == BSP_BTN_CLICK) {
            s_demo = false; s_idle_time = 0;
            arcade_input(&s_game, ARCADE_HOME_REQUEST);
        }
        return;
    }
    if (message.event == BSP_BTN_PRESS || message.event == BSP_BTN_CLICK ||
        message.event == BSP_BTN_LONG) s_idle_time = 0;
    if (s_game.mode == ARCADE_HOME) {
        if (message.event == BSP_BTN_PRESS && message.button != BSP_BTN_OK) {
            int direction = message.button == BSP_BTN_UP ? -1 : 1;
            s_game.kind = (arcade_kind_t)((s_game.kind + ARCADE_GAME_COUNT + direction) % ARCADE_GAME_COUNT);
        } else if (message.button == BSP_BTN_OK && message.event == BSP_BTN_CLICK)
            arcade_start(&s_game, s_game.kind);
        return;
    }
    if (message.event == BSP_BTN_LONG &&
        message.button == (arcade_long_exit_uses_up(s_game.kind) ? BSP_BTN_UP : BSP_BTN_OK)) {
        arcade_input(&s_game, ARCADE_HOME_REQUEST); return;
    }
    if (message.button == BSP_BTN_UP || message.button == BSP_BTN_DOWN) {
        bool left = message.button == BSP_BTN_UP;
        if (message.event == BSP_BTN_PRESS)
            arcade_input(&s_game, left ? ARCADE_LEFT_DOWN : ARCADE_RIGHT_DOWN);
        else if (message.event == BSP_BTN_RELEASE)
            arcade_input(&s_game, left ? ARCADE_LEFT_UP : ARCADE_RIGHT_UP);
    } else if (message.button == BSP_BTN_OK) {
        if (message.event == BSP_BTN_PRESS) arcade_input(&s_game, ARCADE_OK_DOWN);
        else if (message.event == BSP_BTN_RELEASE) arcade_input(&s_game, ARCADE_OK_UP);
        else if (message.event == BSP_BTN_CLICK) arcade_input(&s_game, ARCADE_OK_CLICK);
        else if (message.event == BSP_BTN_DOUBLE) arcade_input(&s_game, ARCADE_OK_DOUBLE);
    }
}
static void battery_task(void *arg) {
    (void)arg;
    for (;;) { s_battery_soc = bsp_battery_soc(); vTaskDelay(pdMS_TO_TICKS(3000)); }
}
static void game_task(void *arg) {
    (void)arg;
    TickType_t previous = xTaskGetTickCount();
    int64_t last_us = esp_timer_get_time(), perf_us = last_us;
    uint32_t updates = 0, old_rendered = 0, old_pixels = 0;
    uint32_t slow_loops = 0, worst_dt_us = 0;
    arcade_kind_t perf_kind = ARCADE_GAME_COUNT;
    int last_battery = -2, battery_ticks = 0;
    for (;;) {
        vTaskDelayUntil(&previous, pdMS_TO_TICKS(33));
        int64_t now = esp_timer_get_time();
        float dt = (now - last_us) / 1000000.0f;
        last_us = now;
        uint32_t dt_us = (uint32_t)(dt * 1000000.0f);
        if (dt_us > worst_dt_us) worst_dt_us = dt_us;
        if (dt_us > 50000) slow_loops++;
        key_message_t message;
        while (xQueueReceive(s_keys, &message, 0) == pdTRUE) handle_key(message);
        if (s_game.mode == ARCADE_HOME && !s_demo) {
            s_idle_time += dt;
            if (s_idle_time >= 20) {
                s_demo = true; s_demo_index = 0; s_demo_time = 0;
                arcade_start(&s_game, ARCADE_BREAKOUT);
            }
        }
        if (s_demo) {
            s_demo_time += dt;
            if (s_demo_time >= 8) {
                s_demo_time = 0;
                if (++s_demo_index >= ARCADE_GAME_COUNT) {
                    s_demo = false; s_idle_time = 0;
                    arcade_input(&s_game, ARCADE_HOME_REQUEST);
                } else arcade_start(&s_game, (arcade_kind_t)s_demo_index);
            } else if (s_game.mode != ARCADE_PLAYING)
                arcade_start(&s_game, s_game.kind);
        }
        uint32_t score_before = s_game.score;
        arcade_mode_t mode_before = s_game.mode;
        arcade_tick(&s_game, dt, s_demo);
        if (bsp_lvgl_lock(5)) {
            refresh_ui(false);
            if (++battery_ticks >= 30) {
                battery_ticks = 0;
                int value = s_battery_soc;
                if (value != last_battery) {
                    last_battery = value;
                    if (value >= 0) lv_label_set_text_fmt(s_battery, "%d%%", value);
                    else lv_label_set_text(s_battery, "--");
                }
            }
            bsp_lvgl_unlock();
        }
        bool active = s_game.mode == ARCADE_PLAYING;
        uint32_t rendered = s_rendered, pixels = s_pixels;
        arcade_kind_t kind = s_game.kind;
        uint32_t score = s_game.score;
        arcade_mode_t mode = s_game.mode;
        if (s_audio_ok && score > score_before && kind != ARCADE_RACER)
            zuma_audio_cue(ZUMA_SOUND_MATCH);
        if (s_audio_ok && mode != mode_before && mode == ARCADE_LOST)
            zuma_audio_cue(ZUMA_SOUND_LOSE);
        if (kind != perf_kind) {
            perf_kind = kind; perf_us = now; updates = 0;
            old_rendered = rendered; old_pixels = pixels;
            slow_loops = 0; worst_dt_us = 0;
        }
        if (active || s_demo) {
            updates++;
            int64_t elapsed = now - perf_us;
            if (elapsed >= 5000000) {
                ESP_LOGI(TAG, "perf game=%s logic=%lu.%01lu fps render=%lu.%01lu fps SPI=%lu px/5s worst=%lu ms slow=%lu free=%lu largest=%lu demo=%d",
                    arcade_name(kind),
                    (unsigned long)(updates * 1000000ULL / elapsed),
                    (unsigned long)((updates * 10000000ULL / elapsed) % 10),
                    (unsigned long)((rendered - old_rendered) * 1000000ULL / elapsed),
                    (unsigned long)(((rendered - old_rendered) * 10000000ULL / elapsed) % 10),
                    (unsigned long)(pixels - old_pixels),
                    (unsigned long)(worst_dt_us / 1000),
                    (unsigned long)slow_loops,
                    (unsigned long)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                    s_demo);
                perf_us = now; updates = 0; old_rendered = rendered; old_pixels = pixels;
                slow_loops = 0; worst_dt_us = 0;
            }
        } else {
            perf_us = now; updates = 0; old_rendered = rendered; old_pixels = pixels;
            slow_loops = 0; worst_dt_us = 0;
        }
    }
}
void app_main(void) {
    if (esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_GPIO))
        key_guard_arm(&s_key_guard, esp_timer_get_time(), 10000000LL);
    arcade_init(&s_game, (uint32_t)esp_timer_get_time());
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display startup failed"); return;
    }
    s_keys = xQueueCreate(16, sizeof(key_message_t));
    if (!s_keys) { ESP_LOGE(TAG, "Input queue allocation failed"); return; }
    if (!bsp_lvgl_lock(1000)) { ESP_LOGE(TAG, "LVGL lock failed"); return; }
    build_ui(); refresh_ui(true); bsp_lvgl_unlock();
    bsp_display_backlight(83);
    if (bsp_button_init(on_key, NULL) != ESP_OK) ESP_LOGE(TAG, "Buttons unavailable");
    else if (s_key_guard.armed)
        key_guard_sync_released(&s_key_guard,
                                bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "Battery gauge unavailable");
    else if (xTaskCreate(battery_task, "arcade_battery", 2048, NULL, 2, NULL) != pdPASS)
        ESP_LOGW(TAG, "Battery task unavailable");
    s_audio_ok = zuma_audio_start();
    if (!s_audio_ok) ESP_LOGW(TAG, "Audio unavailable; continuing silently");
    if (xTaskCreate(game_task, "classic_nine", 8192, NULL, 4, NULL) != pdPASS)
        ESP_LOGE(TAG, "Game task allocation failed");
    else s_input_ready = true;
}
