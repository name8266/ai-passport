/*
 * Passport Pachinko Arcade: full-screen application, not the BSP demo menu.
 * UI work only runs on the LVGL task. Button timer callbacks only enqueue.
 * 40 ms LVGL timer targets 25 game frames/s; real LCD FPS needs device test.
 */
#include "pachinko_engine.h"

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <string.h>

#define PK_INPUT_QUEUE_LEN 12
#define PK_COLOR_BG 0x06101E
#define PK_COLOR_PANEL 0x102843
#define PK_COLOR_CYAN 0x48E3EF
#define PK_COLOR_MAGENTA 0xFF64B5
#define PK_COLOR_GOLD 0xFFD16A
#define PK_COLOR_WHITE 0xF4F6FB

static const char *TAG = "pachinko";
static const char *const MODE_CN[] = {"经典", "限时", "生存"};
static const char *const MODE_EN[] = {"CLASSIC", "RUSH", "SURVIVAL"};
static const int REEL_FACE[PK_REEL_SYMBOLS] = {1, 2, 3, 5, 7};
static const char *const GOAL_NAME[] = {"PEG", "CENTER", "BUMPER"};

/* s_game is owned exclusively by LVGL task after boot-time initialization. */
static pk_state_t s_game;
static QueueHandle_t s_input_queue;
static QueueHandle_t s_storage_queue;
static lv_obj_t *s_screen;
static lv_obj_t *s_board;
static lv_obj_t *s_bumper;
static lv_obj_t *s_balls[PK_MAX_BALLS];
static lv_obj_t *s_aim;
static lv_obj_t *s_score;
static lv_obj_t *s_best;
static lv_obj_t *s_mode;
static lv_obj_t *s_ammo;
static lv_obj_t *s_status;
static lv_obj_t *s_slots;
static lv_obj_t *s_battery;
static lv_obj_t *s_charge;
static lv_obj_t *s_pool;
static lv_obj_t *s_luck;
static lv_obj_t *s_mission;
static lv_obj_t *s_bonus_panel;
static lv_obj_t *s_bonus_title;
static lv_obj_t *s_bonus_value;
static lv_obj_t *s_rank;
static lv_obj_t *s_help_panel;
static uint32_t s_seen_event_seq;
static uint32_t s_seen_spins;
static uint8_t s_reel_anim_ticks;
static lv_obj_t *s_pocket_text[5];
static lv_timer_t *s_timer;
static uint32_t s_last_input_tick;
static uint32_t s_last_saved[PK_MODE_COUNT];
static pk_reward_meta_t s_last_saved_meta;
static bool s_dimmed;

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} pk_input_t;

typedef struct {
    uint32_t best[PK_MODE_COUNT];
    pk_reward_meta_t meta;
} pk_save_t;

/* Create a rectangle in absolute 240x320 coordinates with no default padding. */
static lv_obj_t *panel(lv_obj_t *parent, int x, int y, int w, int h,
                       uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    return obj;
}

static lv_obj_t *caption(lv_obj_t *parent, int x, int y, const char *txt,
                         uint32_t color, const lv_font_t *font)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, txt);
    return obj;
}

/* Validate EVERY UTF-8 codepoint against the actual bundled CJK subset. */
static bool chinese_supported(const char *utf8)
{
    const unsigned char *p = (const unsigned char *)utf8;
    while (*p) {
        uint32_t code;
        unsigned char a = *p++;
        if (a < 0x80) code = a;
        else if ((a & 0xE0) == 0xC0 && (p[0] & 0xC0) == 0x80) {
            code = ((uint32_t)(a & 0x1F) << 6) | (*p++ & 0x3F);
        } else if ((a & 0xF0) == 0xE0 &&
                   p[0] && p[1] &&
                   (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            code = ((uint32_t)(a & 0x0F) << 12) |
                   ((uint32_t)(p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else {
            return false;
        }
        lv_font_glyph_dsc_t glyph = {0};
        if (!lv_font_get_glyph_dsc(&lv_font_source_han_sans_sc_16_cjk,
                                   &glyph, code, 0) || glyph.is_placeholder) {
            return false;
        }
    }
    return true;
}

static void label_bilingual(lv_obj_t *label, const char *cn, const char *en)
{
    bool available = chinese_supported(cn);
    const char *selected = available ? cn : en;
    /* Skip redundant allocations and serial warnings on every HUD update. */
    if (strcmp(lv_label_get_text(label), selected) == 0) return;
    lv_obj_set_style_text_font(label,
        available ? &lv_font_source_han_sans_sc_16_cjk :
                    &lv_font_montserrat_14, 0);
    lv_label_set_text(label, selected);
    if (!available) ESP_LOGW(TAG, "CJK subset lacks glyph for: %s", cn);
}

/* NVS persists only three high scores, never per-frame state or random seeds. */
static void load_best(void)
{
    nvs_handle_t handle;
    if (nvs_open("pk_arcade", NVS_READONLY, &handle) != ESP_OK) return;
    for (int i = 0; i < PK_MODE_COUNT; ++i) {
        char key[8];
        snprintf(key, sizeof(key), "best%d", i);
        uint32_t score = 0;
        if (nvs_get_u32(handle, key, &score) == ESP_OK) {
            s_game.high_score[i] = score;
        }
    }
    pk_reward_meta_t meta = { .pool = PK_POOL_SEED };
    (void)nvs_get_u32(handle, "pool", &meta.pool);
    (void)nvs_get_u32(handle, "total", &meta.lifetime_score);
    (void)nvs_get_u32(handle, "jackpots", &meta.total_jackpots);
    (void)nvs_get_u32(handle, "medals", &meta.medals);
    pk_rewards_import(&s_game.awards, &meta);
    memcpy(s_last_saved, s_game.high_score, sizeof(s_last_saved));
    s_last_saved_meta = s_game.awards.meta;
    nvs_close(handle);
}

/* Writer task keeps potentially slow Flash commits outside LVGL input/tick. */
static void storage_task(void *unused)
{
    (void)unused;
    pk_save_t job;
    while (xQueueReceive(s_storage_queue, &job, portMAX_DELAY) == pdTRUE) {
        nvs_handle_t handle;
        if (nvs_open("pk_arcade", NVS_READWRITE, &handle) != ESP_OK) continue;
        esp_err_t result = ESP_OK;
        for (int i = 0; i < PK_MODE_COUNT; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "best%d", i);
            if (nvs_set_u32(handle, key, job.best[i]) != ESP_OK) result = ESP_FAIL;
        }
        if (result == ESP_OK) result = nvs_set_u32(handle, "pool", job.meta.pool);
        if (result == ESP_OK) result = nvs_set_u32(handle, "total", job.meta.lifetime_score);
        if (result == ESP_OK) result = nvs_set_u32(handle, "jackpots", job.meta.total_jackpots);
        if (result == ESP_OK) result = nvs_set_u32(handle, "medals", job.meta.medals);
        if (result == ESP_OK) result = nvs_commit(handle);
        if (result != ESP_OK) ESP_LOGW(TAG, "best score commit failed");
        nvs_close(handle);
    }
    vTaskDelete(NULL);
}

/* Only enqueue a snapshot at 1-second intervals or game end, reducing wear. */
static void maybe_save(void)
{
    if (!s_storage_queue) return;
    if (s_game.ticks % 25 != 0 && !s_game.finished) return;
    if (memcmp(s_game.high_score, s_last_saved, sizeof(s_last_saved)) == 0 &&
        memcmp(&s_game.awards.meta, &s_last_saved_meta, sizeof(s_last_saved_meta)) == 0) {
        return;
    }
    pk_save_t job = {0};
    memcpy(job.best, s_game.high_score, sizeof(job.best));
    job.meta = s_game.awards.meta;
    if (xQueueOverwrite(s_storage_queue, &job) == pdTRUE) {
        memcpy(s_last_saved, job.best, sizeof(s_last_saved));
        s_last_saved_meta = job.meta;
    }
}

/* Three-key control: arrows aim; OK fires; double OK nudges; long OK changes mode. */
static void process_key(pk_input_t input)
{
    if (input.event == BSP_BTN_CLICK) {
        if (input.button == BSP_BTN_UP) pk_aim(&s_game, -1);
        if (input.button == BSP_BTN_DOWN) pk_aim(&s_game, 1);
        if (input.button == BSP_BTN_OK) {
            if (s_game.finished) {
                pk_start(&s_game, s_game.mode);
                s_seen_event_seq = s_seen_spins = 0;
                s_reel_anim_ticks = 0;
            } else {
                (void)pk_fire(&s_game);
            }
        }
    } else if (input.button == BSP_BTN_OK && input.event == BSP_BTN_DOUBLE) {
        (void)pk_nudge(&s_game);
    } else if (input.button == BSP_BTN_OK && input.event == BSP_BTN_LONG) {
        pk_start(&s_game, (pk_mode_t)((s_game.mode + 1) % PK_MODE_COUNT));
        s_seen_event_seq = s_seen_spins = 0;
        s_reel_anim_ticks = 0;
    }
    if (input.event == BSP_BTN_CLICK || input.event == BSP_BTN_DOUBLE ||
        input.event == BSP_BTN_LONG) {
        s_last_input_tick = s_game.ticks;
        if (s_dimmed) {
            bsp_display_backlight(82);
            s_dimmed = false;
        }
    }
}

/* HUD is refreshed at 5 Hz; balls and moving bumper update at 25 Hz. */
static void refresh_hud(void)
{
    lv_label_set_text_fmt(s_score, "%lu", (unsigned long)s_game.score);
    lv_label_set_text_fmt(s_best, "%lu",
                          (unsigned long)s_game.high_score[s_game.mode]);
    label_bilingual(s_mode, MODE_CN[s_game.mode], MODE_EN[s_game.mode]);
    uint32_t medals = s_game.awards.meta.medals;
    unsigned rank = medals >= 100 ? 4u : medals >= 40 ? 3u :
                    medals >= 10 ? 2u : 1u;
    lv_label_set_text_fmt(s_rank, "R%u", rank);
    if (s_game.mode == PK_RUSH) {
        unsigned remaining = s_game.ticks >= 1500 ? 0 : (1500 - s_game.ticks) / 25;
        lv_label_set_text_fmt(s_ammo, "TIME %02us", remaining);
    } else {
        lv_label_set_text_fmt(s_ammo, "BALL %d", s_game.ammo);
    }
     if (s_game.finished) {
        label_bilingual(s_status, "本局结束", "ROUND END");
    } else if (s_game.fever_ticks) {
        lv_label_set_text_fmt(s_status, "FEVER x3 %us",
                              (unsigned)(s_game.fever_ticks / 25));
    } else {
        lv_label_set_text_fmt(s_status, "COMBO x%d",
                              s_game.combo);
    }
    lv_label_set_text_fmt(s_pool, "JP %lu",
                          (unsigned long)s_game.awards.meta.pool);
    lv_label_set_text_fmt(s_luck, "LUCK %u/%u",
                          (unsigned)s_game.awards.no_big_streak,
                          (unsigned)PK_PITY_SPINS);
    lv_label_set_text_fmt(s_mission, "ST%u %s %u/%u",
                          (unsigned)s_game.awards.mission_stage,
                          GOAL_NAME[s_game.awards.mission_goal],
                          (unsigned)s_game.awards.mission_progress,
                          (unsigned)s_game.awards.mission_target);
    for (int i = 0; i < 5; ++i) {
        const char *value = s_game.mode == PK_SURVIVAL && (i == 0 || i == 4)
                          ? "0" : (i == 2 ? "160" : (i == 1 || i == 3 ? "40" : "10"));
        lv_label_set_text(s_pocket_text[i], value);
    }
    lv_bar_set_value(s_charge, s_game.energy, LV_ANIM_OFF);
    if (s_game.ticks % 25 == 0) {
        int soc = bsp_battery_soc();
        if (soc >= 0) lv_label_set_text_fmt(s_battery, "%d%%", soc);
        else lv_label_set_text(s_battery, "--%");
    }
}

/* All game logic runs from this LVGL timer; no cross-task LVGL calls exist. */
static void game_tick(lv_timer_t *unused)
{
    (void)unused;
    pk_input_t input;
    int processed = 0;
    while (processed++ < PK_INPUT_QUEUE_LEN &&
           xQueueReceive(s_input_queue, &input, 0) == pdTRUE) {
        process_key(input);
    }
    pk_step(&s_game);

    /* Sequential stopping reels create anticipation without blocking physics. */
    if (s_seen_spins != s_game.awards.spins) {
        s_seen_spins = s_game.awards.spins;
        s_reel_anim_ticks = 18;   /* 720 ms total at 25 Hz */
    }
    if (s_reel_anim_ticks) --s_reel_anim_ticks;
    int faces[3];
    for (int i = 0; i < 3; ++i) {
        const int stop_at = 12 - 6 * i;
        if (s_reel_anim_ticks > stop_at) {
            faces[i] = REEL_FACE[(s_game.ticks / 2 + i * 2) % PK_REEL_SYMBOLS];
        } else {
            faces[i] = REEL_FACE[s_game.reels[i]];
        }
    }
    lv_label_set_text_fmt(s_slots, "%d %d %d", faces[0], faces[1], faces[2]);
    lv_obj_set_style_text_color(s_slots, lv_color_hex(
        s_reel_anim_ticks && ((s_game.ticks / 3) & 1) ?
        PK_COLOR_CYAN : PK_COLOR_MAGENTA), 0);
    for (int i = 0; i < PK_MAX_BALLS; ++i) {
        pk_ball_t *ball = &s_game.balls[i];
        if (ball->active) {
            lv_obj_remove_flag(s_balls[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_balls[i], (int)ball->x - 4, (int)ball->y - 4);
        } else {
            lv_obj_add_flag(s_balls[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_set_x(s_aim, (int)s_game.aim_x - 5);
    if (s_game.mode == PK_SURVIVAL) {
        lv_obj_set_x(s_bumper, (int)pk_bumper_x(&s_game) - 11);
    } else {
        lv_obj_set_x(s_bumper, 108);
    }
    if (s_game.ticks % 4 == 0) {
        bool flash = s_game.fever_ticks && ((s_game.ticks / 4) & 1);
        lv_obj_set_style_bg_color(s_bumper,
            lv_color_hex(flash ? PK_COLOR_MAGENTA : PK_COLOR_GOLD), 0);
        lv_obj_set_style_border_color(s_board,
            lv_color_hex(s_game.fever_ticks ? PK_COLOR_MAGENTA : 0x2F648B), 0);
    }
    if (s_game.ticks % 5 == 0 || s_game.finished) refresh_hud();
    /* Celebration is a bounded, high-contrast overlay on the playfield. */
    if (s_game.awards.event_ticks == 0 ||
        (s_reel_anim_ticks && s_game.awards.event != PK_PRIZE_MISSION)) {
        lv_obj_add_flag(s_bonus_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        const pk_rewards_t *a = &s_game.awards;
        lv_obj_remove_flag(s_bonus_panel, LV_OBJ_FLAG_HIDDEN);
        if (a->event_seq != s_seen_event_seq) {
            s_seen_event_seq = a->event_seq;
            const char *name = "WIN";
            switch (a->event) {
            case PK_PRIZE_PAIR: name = "PAIR WIN"; break;
            case PK_PRIZE_MINI: name = "MINI WIN"; break;
            case PK_PRIZE_BIG: name = "BIG BONUS"; break;
            case PK_PRIZE_SUPER: name = "SUPER BONUS"; break;
            case PK_PRIZE_JACKPOT: name = "777 MEGA JACKPOT"; break;
            case PK_PRIZE_MISSION: name = "MISSION CLEAR"; break;
            default: break;
            }
            lv_label_set_text(s_bonus_title, name);
            lv_label_set_text_fmt(s_bonus_value, "+%lu",
                                  (unsigned long)a->event_points);
        }
        if (s_game.ticks % 5 == 0) {
            lv_obj_set_style_border_color(s_bonus_panel,
                lv_color_hex((s_game.ticks / 5) % 2 ? PK_COLOR_GOLD :
                             PK_COLOR_MAGENTA), 0);
        }
    }
    maybe_save();
    if (s_game.shots || s_game.ticks >= 125) {
        lv_obj_add_flag(s_help_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_help_panel, LV_OBJ_FLAG_HIDDEN);
    }

    /* Dim after a minute without any control input; restore on next action. */
    if (!s_dimmed && s_game.ticks - s_last_input_tick >= 1500) {
        bsp_display_backlight(24);
        s_dimmed = true;
    }
}

/* Reusable BSP button runs in esp_timer callback: bounded enqueue ONLY. */
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *unused)
{
    (void)unused;
    if (!s_input_queue) return;
    if (ev != BSP_BTN_CLICK && ev != BSP_BTN_DOUBLE &&
        ev != BSP_BTN_LONG) return;
    const pk_input_t input = { .button = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

/* One purpose-built neon cabinet; never reuse baseline ui_pixel/demo shell. */
static void build_ui(void)
{
    s_screen = panel(NULL, 0, 0, 240, 320, PK_COLOR_BG, 0);

    panel(s_screen, 7, 4, 226, 65, 0x10253F, 11);
    lv_obj_t *title = caption(s_screen, 13, 6, "", PK_COLOR_GOLD,
                              &lv_font_montserrat_14);
    label_bilingual(title, "柏青哥", "PACHINKO");
    caption(s_screen, 13, 29, "SCORE", 0x8DA7C4, &lv_font_montserrat_14);
    caption(s_screen, 136, 29, "BEST", 0x8DA7C4, &lv_font_montserrat_14);
    s_score = caption(s_screen, 63, 29, "0", PK_COLOR_WHITE, &lv_font_montserrat_14);
    s_best = caption(s_screen, 180, 29, "0", PK_COLOR_GOLD, &lv_font_montserrat_14);
    s_mode = caption(s_screen, 13, 49, "", PK_COLOR_CYAN, &lv_font_montserrat_14);
    s_ammo = caption(s_screen, 86, 49, "BALL 30", PK_COLOR_WHITE,
                     &lv_font_montserrat_14);
    s_battery = caption(s_screen, 192, 6, "--%", 0xA8C5DC,
                        &lv_font_montserrat_14);
    s_rank = caption(s_screen, 151, 6, "R1", PK_COLOR_CYAN,
                     &lv_font_montserrat_14);

    s_board = panel(s_screen, 7, 73, 226, 186, PK_COLOR_PANEL, 12);
    lv_obj_set_style_border_width(s_board, 2, 0);
    lv_obj_set_style_border_color(s_board, lv_color_hex(0x2F648B), 0);
    lv_obj_set_style_bg_grad_color(s_board, lv_color_hex(0x071628), 0);
    lv_obj_set_style_bg_grad_dir(s_board, LV_GRAD_DIR_VER, 0);

    /* Aiming rail plus 33 independently drawn stationary steel pins. */
    panel(s_screen, 25, 79, 188, 2, 0x406D8A, 1);
    s_aim = panel(s_screen, 115, 77, 10, 7, PK_COLOR_MAGENTA, 3);
    for (int row = 0; row < PK_PEG_ROWS; ++row) {
        for (int col = 0; col < pk_peg_count(row); ++col) {
            float cx, cy;
            pk_peg_pos(row, col, &cx, &cy);
            uint32_t color = row & 1 ? 0x7DA2BE : 0x94D8E6;
            panel(s_screen, (int)cx - 3, (int)cy - 3, 6, 6, color, LV_RADIUS_CIRCLE);
        }
    }

    /* The luminous target moves only in the SURVIVAL ruleset. */
    s_bumper = panel(s_screen, 108, 159, 22, 22, PK_COLOR_GOLD, LV_RADIUS_CIRCLE);
    lv_obj_set_style_border_color(s_bumper, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(s_bumper, 2, 0);
    panel(s_bumper, 6, 6, 10, 10, 0xB4514D, LV_RADIUS_CIRCLE);

    /* Five scoring mouths: no bitmap/framebuffer allocation required. */
    static const uint32_t pocket_color[] = {
        0x2C6B8C, 0x1789A4, 0xAA568F, 0x1789A4, 0x2C6B8C
    };
    static const char *const pocket_label[] = { "10", "40", "160", "40", "10" };
    for (int i = 0; i < 5; ++i) {
        int x = 13 + i * 43;
        panel(s_screen, x, 240, 41, 17, pocket_color[i], 4);
        s_pocket_text[i] = caption(s_screen, x + (i == 2 ? 5 : 10), 241,
                pocket_label[i], PK_COLOR_WHITE, &lv_font_montserrat_14);
    }

    /* Six simultaneously reusable sprites, each only 8x8 pixels. */
    for (int i = 0; i < PK_MAX_BALLS; ++i) {
        s_balls[i] = panel(s_screen, 0, 0, 8, 8,
                           i & 1 ? PK_COLOR_MAGENTA : PK_COLOR_WHITE,
                           LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_color(s_balls[i], lv_color_hex(PK_COLOR_CYAN), 0);
        lv_obj_set_style_border_width(s_balls[i], 1, 0);
        lv_obj_add_flag(s_balls[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Console HUD: reel, progressive pool, guarantee and level mission. */
    panel(s_screen, 7, 264, 226, 52, 0x10253F, 9);
    s_status = caption(s_screen, 13, 266, "COMBO x1", PK_COLOR_GOLD,
                       &lv_font_montserrat_14);
    panel(s_screen, 155, 265, 72, 18, 0x30194B, 4);
    s_slots = caption(s_screen, 161, 266, "1 1 1", PK_COLOR_MAGENTA,
                      &lv_font_montserrat_14);
    s_pool = caption(s_screen, 13, 283, "JP 500", PK_COLOR_GOLD,
                     &lv_font_montserrat_14);
    s_luck = caption(s_screen, 132, 283, "LUCK 0/12", PK_COLOR_CYAN,
                     &lv_font_montserrat_14);
    s_mission = caption(s_screen, 13, 299, "ST1 PEG 0/12", PK_COLOR_WHITE,
                        &lv_font_montserrat_14);
    s_charge = lv_bar_create(s_screen);
    lv_obj_set_pos(s_charge, 180, 303);
    lv_obj_set_size(s_charge, 43, 7);
    lv_bar_set_range(s_charge, 0, 100);
    lv_obj_set_style_radius(s_charge, 4, 0);
    lv_obj_set_style_bg_color(s_charge, lv_color_hex(0x30475F), 0);
    lv_obj_set_style_bg_color(s_charge, lv_color_hex(PK_COLOR_CYAN),
                              LV_PART_INDICATOR);
    /* Overlay is allocated once: no sprites or fonts created at 25 Hz. */
    s_bonus_panel = panel(s_screen, 22, 127, 196, 60, 0x310E40, 11);
    lv_obj_set_style_border_width(s_bonus_panel, 3, 0);
    lv_obj_set_style_border_color(s_bonus_panel, lv_color_hex(PK_COLOR_GOLD), 0);
    s_bonus_title = caption(s_bonus_panel, 12, 8, "BONUS", PK_COLOR_GOLD,
                            &lv_font_montserrat_14);
    s_bonus_value = caption(s_bonus_panel, 12, 29, "+0", PK_COLOR_WHITE,
                            &lv_font_montserrat_20);
    lv_obj_add_flag(s_bonus_panel, LV_OBJ_FLAG_HIDDEN);
    s_help_panel = panel(s_screen, 13, 86, 214, 50, 0x142C48, 8);
    lv_obj_set_style_border_width(s_help_panel, 1, 0);
    lv_obj_set_style_border_color(s_help_panel,
                                  lv_color_hex(PK_COLOR_CYAN), 0);
    caption(s_help_panel, 10, 5, "UP/DN AIM  OK SHOOT",
            PK_COLOR_WHITE, &lv_font_montserrat_14);
    caption(s_help_panel, 10, 25, "2x OK NUDGE  HOLD MODE",
            PK_COLOR_GOLD, &lv_font_montserrat_14);
    lv_screen_load(s_screen);
    refresh_hud();
    s_timer = lv_timer_create(game_tick, PK_FRAME_MS, NULL);
}

void app_main(void)
{
    pk_init(&s_game, esp_random());

    /* Do not erase NVS on initialization error: the user's old data is owned. */
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_OK) {
        load_best();
        s_storage_queue = xQueueCreate(1, sizeof(pk_save_t));
        if (s_storage_queue &&
            xTaskCreate(storage_task, "pk_store", 3072, NULL, 3, NULL) != pdPASS) {
            vQueueDelete(s_storage_queue);
            s_storage_queue = NULL;
        }
    } else {
        ESP_LOGW(TAG, "NVS unavailable, scores will not survive restart");
    }

    s_input_queue = xQueueCreate(PK_INPUT_QUEUE_LEN, sizeof(pk_input_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return;
    }
    (void)bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display or LVGL startup failed");
        return;
    }
    (void)bsp_battery_init();
    bsp_display_backlight(82);
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "could not lock LVGL to create screen");
        return;
    }
    build_ui();
    bsp_lvgl_unlock();

    esp_err_t button_result = bsp_button_init(on_key, NULL);
    if (button_result != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s", esp_err_to_name(button_result));
    }
    ESP_LOGI(TAG, "Pachinko started: 25 Hz game loop, three game rulesets");
}
