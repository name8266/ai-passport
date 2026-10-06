// Odds Arcade: zero-stakes probability toys for FoloToy AI Passport.
// Four endlessly replayable mini games: slots, roulette, dice and Plinko.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "chance_core.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "motion_core.h"
#include "slot_core.h"

#define INPUT_QUEUE_DEPTH 8
#define AUDIO_QUEUE_DEPTH 16
#define LAMP_COUNT 16
#define PLINKO_ROWS 8
#define PLINKO_PEGS 36

#define C_BG       0x060812
#define C_PANEL    0x11162A
#define C_PANEL_2  0x1B2140
#define C_GOLD     0xFFC857
#define C_GOLD_DIM 0x725A2B
#define C_CYAN     0x5DE4FF
#define C_MAGENTA  0xFF4FA3
#define C_RED      0xFF455D
#define C_GREEN    0x54E391
#define C_TEXT     0xF5F7FF
#define C_MUTED    0x8790AF
#define C_REEL     0xF7F1E3
#define C_INK      0x14151B

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} input_event_t;

typedef enum {
    PAGE_HOME = 0,
    PAGE_SLOT,
    PAGE_ROULETTE,
    PAGE_DICE,
    PAGE_PLINKO,
} page_t;

typedef enum {
    SFX_UI = 1,
    SFX_START,
    SFX_TICK,
    SFX_STOP,
    SFX_PAIR,
    SFX_SPECIAL,
    SFX_BOUNCE,
    SFX_PLINK,
} sfx_t;

typedef struct {
    lv_obj_t *cell;
    lv_obj_t *shape1;
    lv_obj_t *shape2;
    lv_obj_t *shape3;
    lv_obj_t *label;
    uint8_t shown;
    bool stopped;
} reel_view_t;

typedef struct {
    lv_obj_t *body;
    lv_obj_t *pips[7];
} die_view_t;

typedef struct {
    motion_slot_profile_t profile;
    uint32_t phase_q8;
} reel_motion_t;

typedef struct {
    motion_die_profile_t profile;
    int32_t x_q8;
    int32_t y_q8;
    int32_t vx_q8;
    int32_t vy_q8;
    int32_t angle_tenths;
    int32_t omega_tenths;
    uint8_t bounces;
    bool settled;
} die_motion_t;

typedef struct {
    motion_plinko_profile_t profile;
    int32_t x_q8;
    int32_t y_q8;
    int32_t vx_q8;
    int32_t vy_q8;
    int16_t last_peg;
    uint8_t collision_cooldown;
    uint8_t collisions;
} plinko_motion_t;

typedef struct {
    motion_roulette_profile_t profile;
    uint32_t wheel_phase_q8;
    uint32_t ball_phase_q8;
    uint8_t last_ball_pos;
} roulette_motion_t;

typedef struct {
    sfx_t type;
    uint8_t intensity;
    uint8_t variant;
} sfx_event_t;

static const char *TAG = "odds_arcade";
static const char *const SPEED_NAMES[] = { "CHILL", "NORMAL", "TURBO" };

static QueueHandle_t s_input_queue;
static QueueHandle_t s_audio_queue;
static bool s_audio_ready;
static bool s_sound = true;
static volatile bool s_input_ready;

static page_t s_page = PAGE_HOME;
static uint8_t s_home_index;
static uint8_t s_speed = 1;
static bool s_auto;
static bool s_busy;
static uint32_t s_started;
static uint32_t s_next_auto;
static uint32_t s_flash_until;

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_battery;
static lv_obj_t *s_accent_left;
static lv_obj_t *s_accent_right;
static lv_obj_t *s_game_frame;
static lv_timer_t *s_anim_timer;

/* slot */
static reel_view_t s_reels[3];
static slot_result_t s_slot_result;
static reel_motion_t s_reel_motion[3];

/* roulette */
static lv_obj_t *s_wheel_lamps[LAMP_COUNT];
static lv_obj_t *s_roulette_number;
static lv_obj_t *s_roulette_color;
static chance_roulette_result_t s_roulette_result;
static uint8_t s_roulette_pos;
static roulette_motion_t s_roulette_motion;

/* dice */
static die_view_t s_dice[2];
static uint8_t s_die_result[2];
static die_motion_t s_die_motion[2];
static uint32_t s_dice_deadline;
static uint8_t s_dice_pair_cooldown;

/* plinko */
static lv_obj_t *s_plinko_ball;
static lv_obj_t *s_plinko_bins[PLINKO_ROWS + 1];
static uint8_t s_plinko_bin;
static plinko_motion_t s_plinko_motion;

static lv_obj_t *box(lv_obj_t *parent, int x, int y, int w, int h,
                     uint32_t bg, uint32_t border, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(obj, border == bg ? 0 : 1, 0);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    return obj;
}

static void visible(lv_obj_t *obj, bool show)
{
    if (!obj) return;
    if (show) lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static void set_rect(lv_obj_t *obj, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
}

static uint32_t speed_duration(uint32_t chill, uint32_t normal, uint32_t turbo)
{
    if (s_speed == 0) return chill;
    if (s_speed == 2) return turbo;
    return normal;
}

static void tone(uint32_t hz, uint32_t ms, int16_t amplitude)
{
    enum { SAMPLE_RATE = 16000, CHUNK = 192 };
    int16_t pcm[CHUNK];
    uint32_t period = SAMPLE_RATE / hz;
    if (period < 2) period = 2;
    uint32_t total = SAMPLE_RATE * ms / 1000u;
    uint32_t phase = 0;

    while (total) {
        uint32_t n = total > CHUNK ? CHUNK : total;
        for (uint32_t i = 0; i < n; ++i) {
            pcm[i] = phase < period / 2 ? amplitude : (int16_t)-amplitude;
            if (++phase >= period) phase = 0;
        }
        if (bsp_audio_write(pcm, n * sizeof(pcm[0])) != ESP_OK) return;
        total -= n;
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    if (bsp_audio_set_format(16000, 16, 1) != ESP_OK) {
        s_audio_ready = false;
        vTaskDelete(NULL);
        return;
    }
    bsp_audio_set_volume(52);

    for (;;) {
        sfx_event_t event;
        if (xQueueReceive(s_audio_queue, &event, portMAX_DELAY) != pdTRUE) continue;
        if (!s_sound) continue;

        uint32_t variant = event.variant;
        int16_t amp = (int16_t)(2200 + ((uint32_t)event.intensity * 13u));
        if (amp > 5600) amp = 5600;

        switch (event.type) {
        case SFX_UI:
            tone(930 + variant * 27u, 20 + variant * 2u, amp);
            break;
        case SFX_START:
            tone(250 + variant * 18u, 26, amp);
            tone(390 + variant * 24u, 30, amp);
            break;
        case SFX_TICK:
            tone(690 + variant * 41u, 12 + event.intensity / 32u, amp);
            break;
        case SFX_STOP:
            tone(430 + variant * 36u, 24 + event.intensity / 18u, amp);
            break;
        case SFX_PAIR:
            tone(620 + variant * 17u, 52, amp);
            tone(850 + variant * 23u, 70, amp);
            break;
        case SFX_SPECIAL:
            tone(640, 58, amp);
            tone(870, 62, amp);
            tone(1090, 70, amp);
            tone(1310, 105, amp);
            break;
        case SFX_BOUNCE:
            tone(330 + variant * 31u, 18 + event.intensity / 18u, amp);
            tone(500 + variant * 39u, 14 + event.intensity / 28u, amp - 250);
            break;
        case SFX_PLINK:
            tone(820 + variant * 47u, 12 + event.intensity / 28u, amp);
            break;
        default:
            break;
        }
    }
}

static void sfx_intensity(sfx_t type, uint8_t intensity)
{
    if (!s_audio_ready || !s_sound || !s_audio_queue) return;
    sfx_event_t event = {
        .type = type,
        .intensity = intensity,
        .variant = (uint8_t)(esp_random() % 7u),
    };
    (void)xQueueSend(s_audio_queue, &event, 0);
}

static void sfx(sfx_t type)
{
    sfx_intensity(type, 128);
}

static void clear_page_refs(void)
{
    s_title = NULL;
    s_status = NULL;
    s_hint = NULL;
    s_battery = NULL;
    s_accent_left = NULL;
    s_accent_right = NULL;
    s_game_frame = NULL;
    for (int i = 0; i < 3; ++i) {
        s_reels[i].cell = NULL;
        s_reels[i].shape1 = NULL;
        s_reels[i].shape2 = NULL;
        s_reels[i].shape3 = NULL;
        s_reels[i].label = NULL;
    }
    for (int i = 0; i < LAMP_COUNT; ++i) s_wheel_lamps[i] = NULL;
    s_roulette_number = NULL;
    s_roulette_color = NULL;
    for (int d = 0; d < 2; ++d) {
        s_dice[d].body = NULL;
        for (int i = 0; i < 7; ++i) s_dice[d].pips[i] = NULL;
    }
    s_plinko_ball = NULL;
    for (int i = 0; i <= PLINKO_ROWS; ++i) s_plinko_bins[i] = NULL;
}

static void update_battery_unlocked(void)
{
    if (!s_battery) return;
    int soc = bsp_battery_soc();
    if (soc < 0) lv_label_set_text(s_battery, "BAT --");
    else lv_label_set_text_fmt(s_battery, "BAT %d%%", soc);
}

static void build_shell(const char *title_text)
{
    clear_page_refs();
    lv_obj_clean(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(C_BG), 0);

    s_title = label(s_screen, title_text, &lv_font_montserrat_20, C_GOLD);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 9);

    s_accent_left = box(s_screen, 12, 34, 72, 2, C_MAGENTA, C_MAGENTA, 0);
    s_accent_right = box(s_screen, 156, 34, 72, 2, C_CYAN, C_CYAN, 0);

    s_battery = label(s_screen, "BAT --", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(s_battery, LV_ALIGN_BOTTOM_RIGHT, -10, -4);
    update_battery_unlocked();
}

static void set_game_footer(const char *status_text)
{
    s_status = label(s_screen, status_text, &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(s_status, 224);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 230);

    s_hint = label(s_screen, "OK PLAY  ·  UP SPEED  ·  DOWN SOUND", &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(s_hint, 224);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, 258);

    lv_obj_t *home = label(s_screen, "HOLD UP AUTO  ·  HOLD OK HOME", &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(home, 224);
    lv_obj_set_style_text_align(home, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(home, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static void refresh_hint(void)
{
    if (!s_hint || s_page == PAGE_HOME) return;
    lv_label_set_text_fmt(s_hint, "%s  ·  %s  ·  %s",
                          SPEED_NAMES[s_speed],
                          s_auto ? "AUTO ON" : "AUTO OFF",
                          s_sound ? "SOUND ON" : "MUTE");
}

static void flash_machine(uint32_t ms)
{
    s_flash_until = lv_tick_get() + ms;
}

static void render_symbol(reel_view_t *reel, uint8_t symbol)
{
    if (!reel->cell || reel->shown == symbol) return;
    reel->shown = symbol;

    visible(reel->shape1, false);
    visible(reel->shape2, false);
    visible(reel->shape3, false);
    visible(reel->label, false);
    lv_obj_set_style_border_width(reel->shape1, 0, 0);

    switch (symbol) {
    case SLOT_CHERRY:
        set_rect(reel->shape1, 9, 41, 19, 19, C_RED, LV_RADIUS_CIRCLE);
        set_rect(reel->shape2, 31, 36, 19, 19, C_RED, LV_RADIUS_CIRCLE);
        set_rect(reel->shape3, 28, 20, 4, 25, C_GREEN, 2);
        visible(reel->shape1, true);
        visible(reel->shape2, true);
        visible(reel->shape3, true);
        break;
    case SLOT_LEMON:
        set_rect(reel->shape1, 11, 34, 36, 24, 0xFFD84A, 12);
        set_rect(reel->shape2, 36, 25, 12, 7, C_GREEN, 5);
        visible(reel->shape1, true);
        visible(reel->shape2, true);
        break;
    case SLOT_BAR:
        set_rect(reel->shape1, 7, 31, 44, 30, C_INK, 7);
        lv_obj_set_style_border_color(reel->shape1, lv_color_hex(C_GOLD), 0);
        lv_obj_set_style_border_width(reel->shape1, 2, 0);
        lv_label_set_text(reel->label, "BAR");
        lv_obj_set_style_text_color(reel->label, lv_color_hex(C_TEXT), 0);
        lv_obj_align(reel->label, LV_ALIGN_CENTER, 0, 1);
        visible(reel->shape1, true);
        visible(reel->label, true);
        break;
    case SLOT_BELL:
        set_rect(reel->shape1, 14, 25, 30, 34, C_GOLD, 12);
        set_rect(reel->shape2, 9, 53, 40, 7, C_GOLD, 4);
        set_rect(reel->shape3, 25, 60, 9, 7, 0xD59622, LV_RADIUS_CIRCLE);
        visible(reel->shape1, true);
        visible(reel->shape2, true);
        visible(reel->shape3, true);
        break;
    case SLOT_GEM:
        set_rect(reel->shape1, 12, 27, 34, 34, C_CYAN, 8);
        lv_obj_set_style_border_color(reel->shape1, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_border_width(reel->shape1, 2, 0);
        set_rect(reel->shape2, 22, 37, 14, 14, 0x1687B8, 4);
        visible(reel->shape1, true);
        visible(reel->shape2, true);
        break;
    case SLOT_SEVEN:
    default:
        set_rect(reel->shape1, 9, 24, 40, 5, C_GOLD, 3);
        lv_label_set_text(reel->label, "7");
        lv_obj_set_style_text_font(reel->label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(reel->label, lv_color_hex(C_RED), 0);
        lv_obj_align(reel->label, LV_ALIGN_CENTER, 0, 4);
        visible(reel->shape1, true);
        visible(reel->label, true);
        break;
    }
}

static void reel_translate(reel_view_t *reel, int offset)
{
    if (!reel || !reel->cell) return;
    lv_obj_set_style_translate_y(reel->shape1, offset, 0);
    lv_obj_set_style_translate_y(reel->shape2, offset, 0);
    lv_obj_set_style_translate_y(reel->shape3, offset, 0);
    lv_obj_set_style_translate_y(reel->label, offset, 0);
}

static void build_reel(reel_view_t *reel, int x, uint8_t initial)
{
    reel->cell = box(s_game_frame, x, 20, 58, 92, C_REEL, C_GOLD_DIM, 12);
    lv_obj_set_style_shadow_color(reel->cell, lv_color_hex(C_CYAN), 0);
    lv_obj_set_style_shadow_opa(reel->cell, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(reel->cell, 8, 0);

    reel->shape1 = box(reel->cell, 0, 0, 1, 1, C_RED, C_RED, 0);
    reel->shape2 = box(reel->cell, 0, 0, 1, 1, C_RED, C_RED, 0);
    reel->shape3 = box(reel->cell, 0, 0, 1, 1, C_RED, C_RED, 0);
    reel->label = label(reel->cell, "", &lv_font_montserrat_14, C_INK);
    reel->shown = 0xFF;
    reel->stopped = true;
    render_symbol(reel, initial);
}

static void build_slot(void)
{
    build_shell("NEON SLOTS");
    s_game_frame = box(s_screen, 9, 70, 222, 146, C_PANEL_2, C_GOLD, 18);
    lv_obj_set_style_border_width(s_game_frame, 2, 0);
    lv_obj_set_style_shadow_color(s_game_frame, lv_color_hex(C_MAGENTA), 0);
    lv_obj_set_style_shadow_opa(s_game_frame, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(s_game_frame, 12, 0);

    build_reel(&s_reels[0], 11, SLOT_CHERRY);
    build_reel(&s_reels[1], 82, SLOT_BAR);
    build_reel(&s_reels[2], 153, SLOT_SEVEN);

    lv_obj_t *line = box(s_game_frame, 6, 66, 210, 2, C_MAGENTA, C_MAGENTA, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_50, 0);
    set_game_footer("PRESS OK TO SPIN");
    refresh_hint();
}

static void build_roulette(void)
{
    static const int8_t lamp_xy[LAMP_COUNT][2] = {
        {104, 7}, {132, 13}, {153, 29}, {161, 52},
        {153, 77}, {132, 94}, {104, 100}, {76, 94},
        {55, 77}, {47, 52}, {55, 29}, {76, 13},
        {91, 10}, {145, 39}, {118, 96}, {60, 62},
    };

    build_shell("ROULETTE FLOW");
    s_game_frame = box(s_screen, 31, 57, 178, 166, C_PANEL_2, C_GOLD, 22);
    lv_obj_set_style_border_width(s_game_frame, 2, 0);

    lv_obj_t *wheel = box(s_game_frame, 34, 12, 110, 110, 0x101521, C_GOLD_DIM, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(wheel, 4, 0);
    lv_obj_t *inner = box(wheel, 22, 22, 66, 66, 0x071019, C_GREEN, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(inner, 2, 0);

    for (int i = 0; i < LAMP_COUNT; ++i) {
        s_wheel_lamps[i] = box(s_game_frame, lamp_xy[i][0], lamp_xy[i][1], 10, 10,
                               (i & 1) ? C_INK : C_RED, C_GOLD_DIM, LV_RADIUS_CIRCLE);
    }

    s_roulette_number = label(inner, "0", &lv_font_montserrat_20, C_TEXT);
    lv_obj_center(s_roulette_number);
    s_roulette_color = label(s_game_frame, "GREEN", &lv_font_montserrat_14, C_GREEN);
    lv_obj_align(s_roulette_color, LV_ALIGN_BOTTOM_MID, 0, -13);

    set_game_footer("PRESS OK TO SPIN");
    refresh_hint();
}

static void set_die(die_view_t *die, uint8_t value)
{
    static const bool map[6][7] = {
        {0,0,0,1,0,0,0},
        {1,0,0,0,0,0,1},
        {1,0,0,1,0,0,1},
        {1,1,0,0,0,1,1},
        {1,1,0,1,0,1,1},
        {1,1,1,0,1,1,1},
    };
    if (value < 1 || value > 6) value = 1;
    for (int i = 0; i < 7; ++i) visible(die->pips[i], map[value - 1][i]);
}

static void build_die(die_view_t *die, int x)
{
    static const uint8_t pip_xy[7][2] = {
        {12,12}, {52,12}, {12,32}, {32,32}, {52,32}, {12,52}, {52,52},
    };
    die->body = box(s_game_frame, x, 20, 64, 64, C_REEL, C_GOLD_DIM, 14);
    lv_obj_set_style_transform_pivot_x(die->body, 32, 0);
    lv_obj_set_style_transform_pivot_y(die->body, 32, 0);
    for (int i = 0; i < 7; ++i) {
        int px = pip_xy[i][0];
        int py = pip_xy[i][1];
        die->pips[i] = box(die->body, px - 4, py - 4, 9, 9, C_INK, C_INK, LV_RADIUS_CIRCLE);
    }
    set_die(die, 1);
}

static void build_dice(void)
{
    build_shell("DICE BOUNCE");
    s_game_frame = box(s_screen, 18, 66, 204, 151, C_PANEL_2, C_CYAN, 20);
    lv_obj_set_style_border_width(s_game_frame, 2, 0);
    build_die(&s_dice[0], 20);
    build_die(&s_dice[1], 120);
    set_game_footer("PRESS OK TO ROLL");
    refresh_hint();
}

static void build_plinko(void)
{
    build_shell("PLINKO DROP");
    s_game_frame = box(s_screen, 16, 52, 208, 176, C_PANEL_2, C_CYAN, 18);
    lv_obj_set_style_border_width(s_game_frame, 2, 0);

    int peg_index = 0;
    for (int row = 0; row < PLINKO_ROWS; ++row) {
        int count = row + 1;
        int spacing = 19;
        int start_x = 104 - (count - 1) * spacing / 2;
        int y = 16 + row * 16;
        for (int col = 0; col < count && peg_index < PLINKO_PEGS; ++col) {
            lv_obj_t *peg = box(s_game_frame, start_x + col * spacing - 3, y, 7, 7,
                                (row & 1) ? C_GOLD : C_CYAN,
                                C_GOLD, LV_RADIUS_CIRCLE);
            (void)peg;
            ++peg_index;
        }
    }

    for (int i = 0; i <= PLINKO_ROWS; ++i) {
        int x = 14 + i * 20;
        s_plinko_bins[i] = box(s_game_frame, x, 148, 18, 19, C_PANEL, C_GOLD_DIM, 4);
    }

    s_plinko_ball = box(s_game_frame, 100, 5, 11, 11, C_MAGENTA, C_TEXT, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_width(s_plinko_ball, 2, 0);
    set_game_footer("PRESS OK TO DROP");
    refresh_hint();
}

static void home_card(int index, int x, int y, const char *name, const char *tag)
{
    bool selected = index == s_home_index;
    lv_obj_t *card = box(s_screen, x, y, 102, 82,
                         selected ? C_PANEL_2 : C_PANEL,
                         selected ? C_GOLD : 0x29314F, 14);
    lv_obj_set_style_border_width(card, selected ? 2 : 1, 0);
    if (selected) {
        lv_obj_set_style_shadow_color(card, lv_color_hex(index & 1 ? C_CYAN : C_MAGENTA), 0);
        lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
        lv_obj_set_style_shadow_width(card, 10, 0);
    }

    lv_obj_t *number = label(card, index == 0 ? "777" :
                             index == 1 ? "00" :
                             index == 2 ? "6x" : "o", &lv_font_montserrat_20,
                             selected ? C_GOLD : C_MUTED);
    lv_obj_align(number, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t *name_label = label(card, name, &lv_font_montserrat_14, C_TEXT);
    lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 9);
    lv_obj_t *tag_label = label(card, tag, &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(tag_label, LV_ALIGN_BOTTOM_MID, 0, -7);
}

static void build_home(void)
{
    build_shell("ODDS ARCADE");
    lv_obj_t *sub = label(s_screen, "PURE CHANCE · ZERO STAKES", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 43);

    home_card(0, 13, 69, "SLOTS", "SPIN");
    home_card(1, 125, 69, "ROULETTE", "FLOW");
    home_card(2, 13, 161, "DICE", "BOUNCE");
    home_card(3, 125, 161, "PLINKO", "DROP");

    s_status = label(s_screen, "UP/DOWN SELECT · OK ENTER", &lv_font_montserrat_14, C_GOLD);
    lv_obj_set_width(s_status, 220);
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -33);

    s_hint = label(s_screen, s_sound ? "SOUND ON" : "MUTE", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_LEFT, 11, -5);
}

static void load_page(page_t page)
{
    s_page = page;
    s_busy = false;
    s_auto = false;
    s_flash_until = 0;
    s_next_auto = 0;
    switch (page) {
    case PAGE_SLOT: build_slot(); break;
    case PAGE_ROULETTE: build_roulette(); break;
    case PAGE_DICE: build_dice(); break;
    case PAGE_PLINKO: build_plinko(); break;
    case PAGE_HOME:
    default: build_home(); break;
    }
}

static void finish_action(const char *message, bool special)
{
    s_busy = false;
    lv_label_set_text(s_status, message);
    if (special) {
        flash_machine(900);
        sfx(SFX_SPECIAL);
    } else {
        sfx(SFX_STOP);
    }
    if (s_auto) {
        uint32_t base = speed_duration(760, 470, 210);
        uint32_t jitter = speed_duration(420, 280, 160);
        s_next_auto = lv_tick_get() + base + (esp_random() % jitter);
    }
}

static void start_slot(void)
{
    uint32_t seed = esp_random();
    s_slot_result = slot_make_result(esp_random(), esp_random(), esp_random());
    for (int i = 0; i < 3; ++i) {
        s_reels[i].stopped = false;
        s_reel_motion[i].profile = motion_slot_profile(seed ^ esp_random(), s_speed, (uint8_t)i);
        s_reel_motion[i].phase_q8 = s_reel_motion[i].profile.phase_q8;
        reel_translate(&s_reels[i], 0);
    }
    s_busy = true;
    s_started = lv_tick_get();
    lv_label_set_text(s_status, "SPINNING...");
    sfx_intensity(SFX_START, (uint8_t)(120u + (seed & 0x5Fu)));
}

static void animate_slot(uint32_t elapsed)
{
    bool all_stopped = true;

    for (int i = 0; i < 3; ++i) {
        motion_slot_profile_t *p = &s_reel_motion[i].profile;
        uint32_t stop_ms = p->stop_ms;
        uint32_t settle_start = stop_ms > p->settle_ms ? stop_ms - p->settle_ms : 0;

        if (elapsed < stop_ms) {
            all_stopped = false;
            uint64_t linear = ((uint64_t)p->velocity_q8 * elapsed) / 28u;
            uint64_t drag = ((uint64_t)p->drag_q8 * elapsed * elapsed) / 1850u;
            uint32_t phase = p->phase_q8 + (uint32_t)(linear > drag ? linear - drag : linear / 5u);
            s_reel_motion[i].phase_q8 = phase;

            uint8_t rolling = (uint8_t)((phase >> 8) % SLOT_SYMBOL_COUNT);
            render_symbol(&s_reels[i], rolling);

            int offset = ((int)(phase & 0xFFu) - 128) * 7 / 128;
            if (elapsed >= settle_start) {
                uint32_t remain = stop_ms - elapsed;
                int bounce = (int)p->bounce_px * (int)remain / (int)(p->settle_ms ? p->settle_ms : 1u);
                offset = ((elapsed / 42u) & 1u) ? bounce : -bounce;
            }
            reel_translate(&s_reels[i], offset);
            lv_obj_set_style_border_color(s_reels[i].cell, lv_color_hex(C_CYAN), 0);
        } else {
            render_symbol(&s_reels[i], s_slot_result.reels[i]);
            reel_translate(&s_reels[i], 0);
            lv_obj_set_style_border_color(s_reels[i].cell, lv_color_hex(C_GOLD_DIM), 0);
            if (!s_reels[i].stopped) {
                s_reels[i].stopped = true;
                uint8_t intensity = (uint8_t)(130u + (p->bounce_px * 18u));
                sfx_intensity(SFX_STOP, intensity);
            }
        }
    }

    if (all_stopped) {
        if (chance_is_triple(s_slot_result.reels)) {
            finish_action("PERFECT MATCH", true);
        } else if (chance_is_pair(s_slot_result.reels)) {
            finish_action("NICE PAIR", false);
            sfx(SFX_PAIR);
        } else {
            finish_action("SATISFYING.", false);
        }
    }
}

static void roulette_show_result(chance_roulette_result_t result)
{
    lv_label_set_text_fmt(s_roulette_number, "%u", (unsigned)result.number);
    const char *name = result.color == CHANCE_RED ? "RED" :
                       result.color == CHANCE_BLACK ? "BLACK" : "GREEN";
    uint32_t color = result.color == CHANCE_RED ? C_RED :
                     result.color == CHANCE_BLACK ? C_TEXT : C_GREEN;
    lv_label_set_text(s_roulette_color, name);
    lv_obj_set_style_text_color(s_roulette_color, lv_color_hex(color), 0);
}

static void start_roulette(void)
{
    uint32_t seed = esp_random();
    s_roulette_result = chance_roulette(esp_random());
    s_roulette_motion.profile = motion_roulette_profile(seed, s_speed);
    s_roulette_motion.wheel_phase_q8 = (uint32_t)s_roulette_motion.profile.wheel_start << 8;
    s_roulette_motion.ball_phase_q8 = (uint32_t)s_roulette_motion.profile.ball_start << 8;
    s_roulette_motion.last_ball_pos = s_roulette_motion.profile.ball_start;
    s_roulette_pos = s_roulette_motion.profile.ball_start;
    s_busy = true;
    s_started = lv_tick_get();
    lv_label_set_text(s_status, "WHEEL SPINNING...");
    sfx_intensity(SFX_START, (uint8_t)(120u + (seed & 0x5Fu)));
}

static void animate_roulette(uint32_t elapsed)
{
    motion_roulette_profile_t *p = &s_roulette_motion.profile;
    uint32_t duration = p->duration_ms;
    uint32_t t = elapsed > duration ? duration : elapsed;

    uint32_t wheel_v = p->wheel_velocity_q8;
    uint32_t ball_v = p->ball_velocity_q8;
    uint32_t wheel_loss = (uint32_t)p->wheel_drag_q8 * t / 16u;
    uint32_t ball_loss = (uint32_t)p->ball_drag_q8 * t / 14u;
    if (wheel_loss < wheel_v) wheel_v -= wheel_loss; else wheel_v = 18;
    if (ball_loss < ball_v) ball_v -= ball_loss; else ball_v = 12;

    s_roulette_motion.wheel_phase_q8 += wheel_v;
    s_roulette_motion.ball_phase_q8 -= ball_v;

    uint8_t wheel_pos = (uint8_t)((s_roulette_motion.wheel_phase_q8 >> 8) % LAMP_COUNT);
    uint8_t ball_pos = (uint8_t)((s_roulette_motion.ball_phase_q8 >> 8) % LAMP_COUNT);
    s_roulette_pos = ball_pos;

    if (ball_pos != s_roulette_motion.last_ball_pos) {
        uint8_t intensity = (uint8_t)(90u + (ball_v > 150 ? 120u : ball_v / 2u));
        sfx_intensity(SFX_TICK, intensity);
        s_roulette_motion.last_ball_pos = ball_pos;
    }

    for (int i = 0; i < LAMP_COUNT; ++i) {
        uint32_t color = i == ball_pos ? C_GOLD :
            ((((i + wheel_pos) & 1) != 0) ? C_INK : C_RED);
        lv_obj_set_style_bg_color(s_wheel_lamps[i], lv_color_hex(color), 0);
    }

    if (elapsed >= duration) {
        roulette_show_result(s_roulette_result);
        bool special = s_roulette_result.number == 0;
        char text[32];
        const char *name = s_roulette_result.color == CHANCE_RED ? "RED" :
                           s_roulette_result.color == CHANCE_BLACK ? "BLACK" : "GREEN";
        snprintf(text, sizeof(text), "%s %u", name, (unsigned)s_roulette_result.number);
        finish_action(text, special);
    }
}

static void start_dice(void)
{
    uint32_t seed = esp_random();
    s_die_result[0] = chance_die(esp_random());
    s_die_result[1] = chance_die(esp_random());

    for (int i = 0; i < 2; ++i) {
        s_die_motion[i].profile = motion_die_profile(seed ^ esp_random(), (uint8_t)i, s_speed);
        s_die_motion[i].x_q8 = s_die_motion[i].profile.x_q8;
        s_die_motion[i].y_q8 = s_die_motion[i].profile.y_q8;
        s_die_motion[i].vx_q8 = s_die_motion[i].profile.vx_q8;
        s_die_motion[i].vy_q8 = s_die_motion[i].profile.vy_q8;
        s_die_motion[i].angle_tenths = s_die_motion[i].profile.angle_tenths;
        s_die_motion[i].omega_tenths = s_die_motion[i].profile.omega_tenths;
        s_die_motion[i].bounces = 0;
        s_die_motion[i].settled = false;
    }

    s_dice_deadline = speed_duration(3000, 2350, 1650) + (seed % 620u);
    s_dice_pair_cooldown = 0;
    s_busy = true;
    s_started = lv_tick_get();
    lv_label_set_text(s_status, "THROW...");
    sfx_intensity(SFX_START, (uint8_t)(130u + (seed & 0x5Fu)));
}

static int32_t iabs32(int32_t value)
{
    return value < 0 ? -value : value;
}

static void animate_dice(uint32_t elapsed)
{
    const int32_t min_x = 3 << 8;
    const int32_t max_x = 137 << 8;
    const int32_t min_y = 2 << 8;
    const int32_t floor_y = 78 << 8;
    bool all_settled = true;

    for (int i = 0; i < 2; ++i) {
        die_motion_t *m = &s_die_motion[i];
        if (m->settled) continue;
        all_settled = false;

        int32_t impact = 0;
        m->x_q8 += m->vx_q8;
        m->y_q8 += m->vy_q8;
        m->vy_q8 += 30 + (int32_t)s_speed * 5;
        m->angle_tenths += m->omega_tenths;

        if (m->x_q8 < min_x) {
            m->x_q8 = min_x;
            impact = iabs32(m->vx_q8);
            m->vx_q8 = -m->vx_q8 * m->profile.restitution / 256;
            m->omega_tenths = -m->omega_tenths * 3 / 4;
        } else if (m->x_q8 > max_x) {
            m->x_q8 = max_x;
            impact = iabs32(m->vx_q8);
            m->vx_q8 = -m->vx_q8 * m->profile.restitution / 256;
            m->omega_tenths = -m->omega_tenths * 3 / 4;
        }

        if (m->y_q8 < min_y) {
            m->y_q8 = min_y;
            impact = iabs32(m->vy_q8);
            m->vy_q8 = iabs32(m->vy_q8) * m->profile.restitution / 256;
        }

        if (m->y_q8 > floor_y) {
            m->y_q8 = floor_y;
            impact = iabs32(m->vy_q8);
            m->vy_q8 = -m->vy_q8 * m->profile.restitution / 256;
            m->vx_q8 = m->vx_q8 * 225 / 256;
            m->omega_tenths = m->omega_tenths * 205 / 256;
            ++m->bounces;
        }

        if (impact > 70) {
            uint8_t intensity = (uint8_t)(impact > 255 ? 255 : impact);
            sfx_intensity(SFX_BOUNCE, intensity);
        }

        uint8_t face = (uint8_t)(((uint32_t)iabs32(m->angle_tenths) / 430u +
                                  m->profile.face_phase) % 6u) + 1u;
        set_die(&s_dice[i], face);
        lv_obj_set_pos(s_dice[i].body, m->x_q8 >> 8, m->y_q8 >> 8);
        int32_t rotation = m->angle_tenths % 3600;
        if (rotation < 0) rotation += 3600;
        lv_obj_set_style_transform_rotation(s_dice[i].body, rotation, 0);

        if ((m->bounces >= 2 && iabs32(m->vy_q8) < 42 && iabs32(m->vx_q8) < 26) ||
            elapsed >= s_dice_deadline) {
            m->settled = true;
            m->y_q8 = floor_y;
            m->vx_q8 = 0;
            m->vy_q8 = 0;
            m->omega_tenths = 0;
            set_die(&s_dice[i], s_die_result[i]);
            lv_obj_set_pos(s_dice[i].body, m->x_q8 >> 8, floor_y >> 8);
            lv_obj_set_style_transform_rotation(s_dice[i].body, 0, 0);
            sfx_intensity(SFX_STOP, (uint8_t)(120u + m->bounces * 18u));
        }
    }

    if (s_dice_pair_cooldown > 0) --s_dice_pair_cooldown;
    if (!s_die_motion[0].settled && !s_die_motion[1].settled && s_dice_pair_cooldown == 0) {
        int32_t dx = (s_die_motion[0].x_q8 - s_die_motion[1].x_q8) >> 8;
        int32_t dy = (s_die_motion[0].y_q8 - s_die_motion[1].y_q8) >> 8;
        if (iabs32(dx) < 56 && iabs32(dy) < 54) {
            int32_t temp = s_die_motion[0].vx_q8;
            s_die_motion[0].vx_q8 = s_die_motion[1].vx_q8;
            s_die_motion[1].vx_q8 = temp;
            s_die_motion[0].vx_q8 += dx >= 0 ? 42 : -42;
            s_die_motion[1].vx_q8 -= dx >= 0 ? 42 : -42;
            s_die_motion[0].omega_tenths = -s_die_motion[0].omega_tenths;
            s_die_motion[1].omega_tenths = -s_die_motion[1].omega_tenths;
            s_die_motion[0].x_q8 += dx >= 0 ? (5 << 8) : -(5 << 8);
            s_die_motion[1].x_q8 -= dx >= 0 ? (5 << 8) : -(5 << 8);
            s_dice_pair_cooldown = 4;
            sfx_intensity(SFX_BOUNCE, 190);
        }
    }

    if (s_die_motion[0].settled && s_die_motion[1].settled) all_settled = true;
    if (all_settled) {
        char text[32];
        if (s_die_result[0] == s_die_result[1]) {
            snprintf(text, sizeof(text), "DOUBLES %u", (unsigned)s_die_result[0]);
            finish_action(text, true);
        } else {
            snprintf(text, sizeof(text), "TOTAL %u",
                     (unsigned)(s_die_result[0] + s_die_result[1]));
            finish_action(text, false);
        }
    }
}

static void plinko_place_ball(void)
{
    lv_obj_set_pos(s_plinko_ball,
                   s_plinko_motion.x_q8 >> 8,
                   s_plinko_motion.y_q8 >> 8);
}

static void start_plinko(void)
{
    uint32_t seed = esp_random();
    s_plinko_motion.profile = motion_plinko_profile(seed, s_speed);
    s_plinko_motion.x_q8 = s_plinko_motion.profile.x_q8;
    s_plinko_motion.y_q8 = s_plinko_motion.profile.y_q8;
    s_plinko_motion.vx_q8 = s_plinko_motion.profile.vx_q8;
    s_plinko_motion.vy_q8 = s_plinko_motion.profile.vy_q8;
    s_plinko_motion.last_peg = -1;
    s_plinko_motion.collision_cooldown = 0;
    s_plinko_motion.collisions = 0;
    s_plinko_bin = 0;

    for (int i = 0; i <= PLINKO_ROWS; ++i)
        lv_obj_set_style_bg_color(s_plinko_bins[i], lv_color_hex(C_PANEL), 0);

    plinko_place_ball();
    s_busy = true;
    s_started = lv_tick_get();
    lv_label_set_text(s_status, "DROP...");
    sfx_intensity(SFX_START, (uint8_t)(100u + (seed & 0x7Fu)));
}

static void animate_plinko(uint32_t elapsed)
{
    plinko_motion_t *m = &s_plinko_motion;
    const int32_t left = 3 << 8;
    const int32_t right = 194 << 8;
    const int32_t floor_y = 137 << 8;

    m->x_q8 += m->vx_q8;
    m->y_q8 += m->vy_q8;
    m->vy_q8 += m->profile.gravity_q8;

    if (m->x_q8 < left) {
        m->x_q8 = left;
        m->vx_q8 = -m->vx_q8 * 190 / 256;
        sfx_intensity(SFX_PLINK, 110);
    } else if (m->x_q8 > right) {
        m->x_q8 = right;
        m->vx_q8 = -m->vx_q8 * 190 / 256;
        sfx_intensity(SFX_PLINK, 110);
    }

    if (m->collision_cooldown > 0) --m->collision_cooldown;

    int ball_x = (m->x_q8 >> 8) + 5;
    int ball_y = (m->y_q8 >> 8) + 5;
    int peg_index = 0;
    bool collided = false;

    for (int row = 0; row < PLINKO_ROWS && !collided; ++row) {
        int count = row + 1;
        int spacing = 19;
        int start_x = 104 - (count - 1) * spacing / 2;
        int peg_y = 19 + row * 16;
        for (int col = 0; col < count; ++col, ++peg_index) {
            int peg_x = start_x + col * spacing;
            int dx = ball_x - peg_x;
            int dy = ball_y - peg_y;
            int dist2 = dx * dx + dy * dy;
            if (dist2 <= 74 && m->vy_q8 > 0 &&
                (m->collision_cooldown == 0 || m->last_peg != peg_index)) {
                int32_t impact = iabs32(m->vy_q8);
                int side;
                if (dx > 1) side = 1;
                else if (dx < -1) side = -1;
                else side = (esp_random() & 1u) ? 1 : -1;

                int32_t jitter_range = m->profile.jitter_q8;
                int32_t jitter = (int32_t)(esp_random() % (uint32_t)(jitter_range * 2 + 1)) -
                                  jitter_range;
                m->vx_q8 += side * (int32_t)m->profile.peg_kick_q8 + jitter;
                if (m->vx_q8 > 300) m->vx_q8 = 300;
                if (m->vx_q8 < -300) m->vx_q8 = -300;

                m->vy_q8 = -(impact * m->profile.restitution / 256) - 18;
                m->y_q8 -= 2 << 8;
                m->last_peg = (int16_t)peg_index;
                m->collision_cooldown = 3;
                ++m->collisions;

                uint8_t intensity = (uint8_t)(impact > 300 ? 255 : 85 + impact / 2);
                sfx_intensity(SFX_PLINK, intensity);
                collided = true;
                break;
            }
        }
    }

    plinko_place_ball();

    if (m->y_q8 >= floor_y || elapsed > 6500u) {
        int x = (m->x_q8 >> 8) + 5;
        int bin = (x - 14 + 10) / 20;
        if (bin < 0) bin = 0;
        if (bin > PLINKO_ROWS) bin = PLINKO_ROWS;
        s_plinko_bin = (uint8_t)bin;
        m->y_q8 = floor_y;
        plinko_place_ball();

        lv_obj_set_style_bg_color(s_plinko_bins[s_plinko_bin], lv_color_hex(C_MAGENTA), 0);
        char text[32];
        if (s_plinko_bin == PLINKO_ROWS / 2) {
            snprintf(text, sizeof(text), "CENTER · %u HITS", (unsigned)m->collisions);
            finish_action(text, true);
        } else {
            snprintf(text, sizeof(text), "BIN %u · %u HITS",
                     (unsigned)(s_plinko_bin + 1), (unsigned)m->collisions);
            finish_action(text, false);
        }
    }
}

static void start_current_game(void)
{
    if (s_busy || s_page == PAGE_HOME) return;
    switch (s_page) {
    case PAGE_SLOT: start_slot(); break;
    case PAGE_ROULETTE: start_roulette(); break;
    case PAGE_DICE: start_dice(); break;
    case PAGE_PLINKO: start_plinko(); break;
    default: break;
    }
}

static void animate_timer(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();

    if (s_accent_left && s_accent_right) {
        bool pulse = ((now / 320u) & 1u) != 0;
        lv_obj_set_style_bg_color(s_accent_left, lv_color_hex(pulse ? C_MAGENTA : C_GOLD_DIM), 0);
        lv_obj_set_style_bg_color(s_accent_right, lv_color_hex(pulse ? C_CYAN : C_GOLD_DIM), 0);
    }

    if (s_flash_until != 0 && s_game_frame) {
        bool active = (int32_t)(s_flash_until - now) > 0;
        lv_obj_set_style_border_color(s_game_frame,
            lv_color_hex(active && ((now / 90u) & 1u) ? C_MAGENTA : C_GOLD), 0);
        if (!active) s_flash_until = 0;
    }

    if (s_busy) {
        uint32_t elapsed = lv_tick_elaps(s_started);
        switch (s_page) {
        case PAGE_SLOT: animate_slot(elapsed); break;
        case PAGE_ROULETTE: animate_roulette(elapsed); break;
        case PAGE_DICE: animate_dice(elapsed); break;
        case PAGE_PLINKO: animate_plinko(elapsed); break;
        default: break;
        }
    } else if (s_auto && s_page != PAGE_HOME && s_next_auto != 0 &&
               (int32_t)(now - s_next_auto) >= 0) {
        s_next_auto = 0;
        start_current_game();
    }
}

static void enter_selected(void)
{
    static const page_t pages[4] = { PAGE_SLOT, PAGE_ROULETTE, PAGE_DICE, PAGE_PLINKO };
    load_page(pages[s_home_index]);
    sfx(SFX_UI);
}

static void handle_input(const input_event_t *input)
{
    if (s_page == PAGE_HOME) {
        if (input->event == BSP_BTN_CLICK) {
            if (input->button == BSP_BTN_UP)
                s_home_index = (uint8_t)((s_home_index + 3u) % 4u);
            else if (input->button == BSP_BTN_DOWN)
                s_home_index = (uint8_t)((s_home_index + 1u) % 4u);
            else if (input->button == BSP_BTN_OK) {
                enter_selected();
                return;
            }
            build_home();
            sfx(SFX_UI);
        } else if (input->event == BSP_BTN_LONG && input->button == BSP_BTN_DOWN) {
            s_sound = !s_sound;
            build_home();
            if (s_sound) sfx(SFX_UI);
        }
        return;
    }

    if (input->event == BSP_BTN_LONG) {
        if (input->button == BSP_BTN_OK) {
            load_page(PAGE_HOME);
            sfx(SFX_UI);
            return;
        }
        if (input->button == BSP_BTN_UP) {
            s_auto = !s_auto;
            if (s_auto && !s_busy) s_next_auto = lv_tick_get() + 250;
            else if (!s_auto) s_next_auto = 0;
            refresh_hint();
            sfx(SFX_UI);
        } else if (input->button == BSP_BTN_DOWN) {
            s_sound = !s_sound;
            refresh_hint();
            if (s_sound) sfx(SFX_UI);
        }
        return;
    }

    if (input->event != BSP_BTN_CLICK) return;
    if (input->button == BSP_BTN_OK) {
        start_current_game();
    } else if (input->button == BSP_BTN_UP && !s_busy) {
        s_speed = (uint8_t)((s_speed + 1u) % 3u);
        refresh_hint();
        sfx(SFX_UI);
    } else if (input->button == BSP_BTN_DOWN) {
        s_sound = !s_sound;
        refresh_hint();
        if (s_sound) sfx(SFX_UI);
    }
}

static void input_task(void *arg)
{
    (void)arg;
    int64_t battery_due = 0;
    for (;;) {
        input_event_t input;
        if (xQueueReceive(s_input_queue, &input, pdMS_TO_TICKS(60)) == pdTRUE) {
            if (bsp_lvgl_lock(300)) {
                handle_input(&input);
                bsp_lvgl_unlock();
            }
        }

        int64_t now = (int64_t)lv_tick_get();
        if (now >= battery_due) {
            battery_due = now + 30000;
            if (bsp_lvgl_lock(250)) {
                update_battery_unlocked();
                bsp_lvgl_unlock();
            }
        }
    }
}

static void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!s_input_ready || !s_input_queue) return;
    input_event_t input = { .button = button, .event = event };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Odds Arcade starting");

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL initialization failed");
        return;
    }
    bsp_display_backlight(100);
    (void)bsp_battery_init();

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(sfx_event_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return;
    }

    if (bsp_audio_init() == ESP_OK && s_audio_queue) {
        s_audio_ready = true;
        if (xTaskCreate(audio_task, "arcade_audio", 4096, NULL, 4, NULL) != pdPASS)
            s_audio_ready = false;
    }

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "cannot lock LVGL");
        return;
    }
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_screen_load(s_screen);
    build_home();
    s_anim_timer = lv_timer_create(animate_timer, 32, NULL);
    bsp_lvgl_unlock();

    if (bsp_button_init(on_button, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button initialization failed");
        return;
    }
    if (xTaskCreate(input_task, "arcade_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "input task allocation failed");
        return;
    }

    s_input_ready = true;
    ESP_LOGI(TAG, "Odds Arcade ready: slots roulette dice plinko");
}
