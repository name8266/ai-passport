// Neon Jackpot: standalone slot-machine game for FoloToy AI Passport.
// Virtual credits only. Three physical buttons control bet and spin.
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "slot_core.h"

#define INPUT_QUEUE_DEPTH 8
#define AUDIO_QUEUE_DEPTH 8
#define STORE_VERSION 1u
#define STARTING_CREDITS 1000u
#define REFILL_CREDITS 500u
#define MIN_BET 10u
#define MAX_BET 100u
#define BET_STEP 10u
#define CREDIT_LIMIT 9999999u

#define C_BG       0x070913
#define C_PANEL    0x11162A
#define C_PANEL_2  0x1A2140
#define C_GOLD     0xFFC857
#define C_GOLD_DIM 0x765C29
#define C_CYAN     0x5DE4FF
#define C_MAGENTA  0xFF4FA3
#define C_RED      0xFF455D
#define C_GREEN    0x54E391
#define C_TEXT     0xF5F7FF
#define C_MUTED    0x8A94B8
#define C_REEL     0xF7F1E3
#define C_INK      0x14151B

typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} input_event_t;

typedef enum {
    SFX_TAP = 1,
    SFX_REEL_STOP,
    SFX_WIN,
    SFX_JACKPOT,
} sfx_t;

typedef struct {
    uint32_t version;
    uint32_t credits;
    uint32_t best_credits;
    uint32_t total_spins;
    uint32_t total_wins;
    uint32_t jackpots;
    uint32_t bet;
    uint8_t sound_enabled;
    uint8_t reserved[3];
} game_store_t;

typedef struct {
    lv_obj_t *cell;
    lv_obj_t *shape1;
    lv_obj_t *shape2;
    lv_obj_t *shape3;
    lv_obj_t *label;
    uint8_t shown;
    bool stopped;
} reel_view_t;

static const char *TAG = "neon_slot";

static QueueHandle_t s_input_queue;
static QueueHandle_t s_audio_queue;
static nvs_handle_t s_nvs;
static bool s_nvs_ready;
static bool s_audio_ready;
static volatile bool s_input_ready;
static volatile bool s_save_pending;

static game_store_t s_store;
static slot_result_t s_result;
static bool s_spinning;
static bool s_stats_mode;
static uint32_t s_spin_started;
static uint32_t s_flash_until;

static lv_obj_t *s_screen;
static lv_obj_t *s_machine;
static lv_obj_t *s_credits_label;
static lv_obj_t *s_bet_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_message_label;
static lv_obj_t *s_submessage_label;
static lv_obj_t *s_sound_label;
static lv_obj_t *s_stats_panel;
static lv_obj_t *s_stats_label;
static lv_timer_t *s_spin_timer;
static reel_view_t s_reels[3];

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

static void render_symbol(reel_view_t *reel, uint8_t symbol)
{
    if (reel->shown == symbol) return;
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

static void update_header(void)
{
    lv_label_set_text_fmt(s_credits_label, "CREDITS %lu", (unsigned long)s_store.credits);
    lv_label_set_text_fmt(s_bet_label, "BET %lu", (unsigned long)s_store.bet);
    lv_label_set_text(s_sound_label, s_store.sound_enabled ? "SND" : "MUTE");
}

static void update_stats_panel(void)
{
    if (!s_stats_panel || !s_stats_label) return;
    lv_label_set_text_fmt(s_stats_label,
        "PLAYER STATS\n\nSPINS   %lu\nWINS    %lu\nJACKPOT %lu\nBEST    %lu\n\nHOLD OK TO CLOSE",
        (unsigned long)s_store.total_spins,
        (unsigned long)s_store.total_wins,
        (unsigned long)s_store.jackpots,
        (unsigned long)s_store.best_credits);
}

static void show_stats(bool show)
{
    s_stats_mode = show;
    visible(s_stats_panel, show);
    if (show) update_stats_panel();
}

static void game_defaults(void)
{
    memset(&s_store, 0, sizeof(s_store));
    s_store.version = STORE_VERSION;
    s_store.credits = STARTING_CREDITS;
    s_store.best_credits = STARTING_CREDITS;
    s_store.bet = MIN_BET;
    s_store.sound_enabled = 1;
}

static void sanitize_store(void)
{
    if (s_store.version != STORE_VERSION) {
        game_defaults();
        return;
    }
    if (s_store.credits > CREDIT_LIMIT) s_store.credits = CREDIT_LIMIT;
    if (s_store.best_credits < s_store.credits) s_store.best_credits = s_store.credits;
    if (s_store.best_credits > CREDIT_LIMIT) s_store.best_credits = CREDIT_LIMIT;
    if (s_store.bet < MIN_BET || s_store.bet > MAX_BET || (s_store.bet % BET_STEP) != 0)
        s_store.bet = MIN_BET;
    s_store.sound_enabled = s_store.sound_enabled ? 1 : 0;
}

static void store_init(void)
{
    game_defaults();
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init unavailable: %s; progress will not persist",
                 esp_err_to_name(err));
        return;
    }
    err = nvs_open("neonslot", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    size_t size = sizeof(s_store);
    game_store_t loaded;
    err = nvs_get_blob(s_nvs, "state", &loaded, &size);
    if (err == ESP_OK && size == sizeof(loaded)) s_store = loaded;
    else if (err != ESP_ERR_NVS_NOT_FOUND) ESP_LOGW(TAG, "NVS state read failed: %s", esp_err_to_name(err));
    sanitize_store();
    s_nvs_ready = true;
}

static void save_state(void)
{
    if (!s_nvs_ready) return;
    esp_err_t err = nvs_set_blob(s_nvs, "state", &s_store, sizeof(s_store));
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    if (err != ESP_OK) ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(err));
}

static void tone(uint32_t hz, uint32_t ms, int16_t amplitude)
{
    enum { SAMPLE_RATE = 16000, CHUNK = 256 };
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
    bsp_audio_set_volume(55);

    for (;;) {
        sfx_t sfx;
        if (xQueueReceive(s_audio_queue, &sfx, portMAX_DELAY) != pdTRUE) continue;
        if (!s_store.sound_enabled) continue;
        switch (sfx) {
        case SFX_TAP:
            tone(900, 25, 3500);
            break;
        case SFX_REEL_STOP:
            tone(620, 35, 4200);
            break;
        case SFX_WIN:
            tone(660, 70, 4500);
            tone(880, 80, 4500);
            tone(1100, 110, 5000);
            break;
        case SFX_JACKPOT:
            tone(660, 80, 5200);
            tone(880, 80, 5200);
            tone(1100, 90, 5400);
            tone(1320, 150, 5600);
            break;
        default:
            break;
        }
    }
}

static void sfx(sfx_t command)
{
    if (!s_audio_ready || !s_store.sound_enabled || !s_audio_queue) return;
    (void)xQueueSend(s_audio_queue, &command, 0);
}

static void update_battery(void)
{
    if (!s_battery_label) return;
    int soc = bsp_battery_soc();
    if (!bsp_lvgl_lock(250)) return;
    if (soc < 0) lv_label_set_text(s_battery_label, "BAT --");
    else lv_label_set_text_fmt(s_battery_label, "BAT %d%%", soc);
    bsp_lvgl_unlock();
}

static void settle_spin(void)
{
    uint32_t payout = slot_payout(s_store.bet, s_result.reels);
    uint64_t total = (uint64_t)s_store.credits + payout;
    s_store.credits = total > CREDIT_LIMIT ? CREDIT_LIMIT : (uint32_t)total;

    if (payout > 0) {
        ++s_store.total_wins;
        if (s_result.jackpot) ++s_store.jackpots;
    }
    if (s_store.credits > s_store.best_credits) s_store.best_credits = s_store.credits;

    if (s_result.jackpot) {
        lv_label_set_text_fmt(s_message_label, "JACKPOT! +%lu", (unsigned long)payout);
        lv_label_set_text(s_submessage_label, "TRIPLE 7 · x50");
        s_flash_until = lv_tick_get() + 1200;
        sfx(SFX_JACKPOT);
    } else if (payout > 0) {
        lv_label_set_text_fmt(s_message_label, "WIN +%lu", (unsigned long)payout);
        lv_label_set_text_fmt(s_submessage_label, "PAYOUT x%lu", (unsigned long)s_result.multiplier);
        s_flash_until = lv_tick_get() + 650;
        sfx(SFX_WIN);
    } else if (s_store.credits < MIN_BET) {
        lv_label_set_text(s_message_label, "OUT OF CHIPS");
        lv_label_set_text(s_submessage_label, "HOLD OK · FREE 500");
    } else {
        lv_label_set_text(s_message_label, "NO WIN");
        lv_label_set_text(s_submessage_label, "OK TO SPIN AGAIN");
    }
    update_header();
    update_stats_panel();
    s_save_pending = true;
}

static void spin_timer(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();

    if (s_flash_until != 0) {
        bool active = (int32_t)(s_flash_until - now) > 0;
        lv_obj_set_style_border_color(s_machine,
            lv_color_hex(active && ((now / 100u) & 1u) ? C_MAGENTA : C_GOLD), 0);
        if (!active) s_flash_until = 0;
    }

    if (!s_spinning) return;

    uint32_t elapsed = lv_tick_elaps(s_spin_started);
    static const uint32_t stop_ms[3] = { 820, 1120, 1420 };

    for (int i = 0; i < 3; ++i) {
        if (elapsed < stop_ms[i]) {
            uint8_t rolling = (uint8_t)((elapsed / 55u + (uint32_t)i * 2u) % SLOT_SYMBOL_COUNT);
            render_symbol(&s_reels[i], rolling);
            lv_obj_set_style_border_color(s_reels[i].cell, lv_color_hex(C_CYAN), 0);
        } else {
            render_symbol(&s_reels[i], s_result.reels[i]);
            lv_obj_set_style_border_color(s_reels[i].cell, lv_color_hex(C_GOLD_DIM), 0);
            if (!s_reels[i].stopped) {
                s_reels[i].stopped = true;
                sfx(SFX_REEL_STOP);
            }
        }
    }

    if (elapsed >= stop_ms[2]) {
        s_spinning = false;
        settle_spin();
    }
}

static void begin_spin(void)
{
    if (s_stats_mode) {
        show_stats(false);
        return;
    }
    if (s_spinning) return;
    if (s_store.credits < s_store.bet) {
        lv_label_set_text(s_message_label, "NOT ENOUGH CHIPS");
        lv_label_set_text(s_submessage_label, "LOWER BET OR HOLD OK IF EMPTY");
        return;
    }

    s_store.credits -= s_store.bet;
    ++s_store.total_spins;
    s_result = slot_make_result(esp_random(), esp_random(), esp_random());
    s_spinning = true;
    s_spin_started = lv_tick_get();
    s_flash_until = 0;
    for (int i = 0; i < 3; ++i) s_reels[i].stopped = false;

    lv_label_set_text(s_message_label, "GOOD LUCK");
    lv_label_set_text(s_submessage_label, "REELS SPINNING...");
    lv_obj_set_style_border_color(s_machine, lv_color_hex(C_CYAN), 0);
    update_header();
    sfx(SFX_TAP);
}

static void change_bet(int direction)
{
    if (s_spinning || s_stats_mode) return;
    int bet = (int)s_store.bet + direction * (int)BET_STEP;
    if (bet > (int)MAX_BET) bet = (int)MIN_BET;
    if (bet < (int)MIN_BET) bet = (int)MAX_BET;
    s_store.bet = (uint32_t)bet;
    update_header();
    lv_label_set_text_fmt(s_message_label, "BET %lu", (unsigned long)s_store.bet);
    lv_label_set_text(s_submessage_label, "OK TO SPIN");
    s_save_pending = true;
    sfx(SFX_TAP);
}

static void handle_input(const input_event_t *input)
{
    if (input->event == BSP_BTN_CLICK) {
        if (!bsp_lvgl_lock(250)) return;
        if (input->button == BSP_BTN_UP) change_bet(+1);
        else if (input->button == BSP_BTN_DOWN) change_bet(-1);
        else if (input->button == BSP_BTN_OK) begin_spin();
        bsp_lvgl_unlock();
        return;
    }

    if (input->event != BSP_BTN_LONG || s_spinning) return;
    if (!bsp_lvgl_lock(250)) return;

    if (input->button == BSP_BTN_UP) {
        s_store.bet = MAX_BET;
        update_header();
        lv_label_set_text(s_message_label, "MAX BET");
        lv_label_set_text(s_submessage_label, "100 CREDITS");
        s_save_pending = true;
        sfx(SFX_TAP);
    } else if (input->button == BSP_BTN_DOWN) {
        s_store.sound_enabled = !s_store.sound_enabled;
        update_header();
        lv_label_set_text(s_message_label, s_store.sound_enabled ? "SOUND ON" : "SOUND OFF");
        lv_label_set_text(s_submessage_label, "HOLD DOWN TO TOGGLE");
        s_save_pending = true;
        if (s_store.sound_enabled) sfx(SFX_TAP);
    } else if (input->button == BSP_BTN_OK) {
        if (s_store.credits < MIN_BET) {
            s_store.credits = REFILL_CREDITS;
            if (s_store.best_credits < s_store.credits) s_store.best_credits = s_store.credits;
            update_header();
            lv_label_set_text(s_message_label, "FREE REFILL +500");
            lv_label_set_text(s_submessage_label, "VIRTUAL CHIPS · HAVE FUN");
            s_save_pending = true;
            sfx(SFX_WIN);
        } else {
            show_stats(!s_stats_mode);
        }
    }
    bsp_lvgl_unlock();
}

static void input_task(void *arg)
{
    (void)arg;
    int64_t battery_due = 0;
    for (;;) {
        input_event_t input;
        if (xQueueReceive(s_input_queue, &input, pdMS_TO_TICKS(50)) == pdTRUE)
            handle_input(&input);

        if (s_save_pending) {
            s_save_pending = false;
            save_state();
        }

        int64_t now = (int64_t)lv_tick_get();
        if (now >= battery_due) {
            battery_due = now + 30000;
            update_battery();
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

static void build_reel(reel_view_t *reel, int x, uint8_t initial)
{
    reel->cell = box(s_machine, x, 21, 58, 88, C_REEL, C_GOLD_DIM, 12);
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

static void build_ui(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    lv_obj_t *title = label(s_screen, "NEON JACKPOT", &lv_font_montserrat_20, C_GOLD);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *accent_l = box(s_screen, 14, 34, 63, 2, C_MAGENTA, C_MAGENTA, 0);
    lv_obj_t *accent_r = box(s_screen, 163, 34, 63, 2, C_CYAN, C_CYAN, 0);
    (void)accent_l;
    (void)accent_r;

    lv_obj_t *credit_pill = box(s_screen, 9, 43, 104, 28, C_PANEL, C_GOLD_DIM, 14);
    s_credits_label = label(credit_pill, "", &lv_font_montserrat_14, C_TEXT);
    lv_obj_center(s_credits_label);

    lv_obj_t *bet_pill = box(s_screen, 118, 43, 62, 28, C_PANEL, C_GOLD_DIM, 14);
    s_bet_label = label(bet_pill, "", &lv_font_montserrat_14, C_GOLD);
    lv_obj_center(s_bet_label);

    lv_obj_t *sound_pill = box(s_screen, 184, 43, 47, 28, C_PANEL, C_GOLD_DIM, 14);
    s_sound_label = label(sound_pill, "", &lv_font_montserrat_14, C_MUTED);
    lv_obj_center(s_sound_label);

    s_machine = box(s_screen, 9, 79, 222, 132, C_PANEL_2, C_GOLD, 18);
    lv_obj_set_style_border_width(s_machine, 2, 0);
    lv_obj_set_style_shadow_color(s_machine, lv_color_hex(C_MAGENTA), 0);
    lv_obj_set_style_shadow_opa(s_machine, LV_OPA_20, 0);
    lv_obj_set_style_shadow_width(s_machine, 12, 0);

    build_reel(&s_reels[0], 11, SLOT_CHERRY);
    build_reel(&s_reels[1], 82, SLOT_BAR);
    build_reel(&s_reels[2], 153, SLOT_SEVEN);

    lv_obj_t *payline = box(s_machine, 6, 64, 210, 2, C_MAGENTA, C_MAGENTA, 0);
    lv_obj_set_style_bg_opa(payline, LV_OPA_50, 0);

    s_message_label = label(s_screen, "READY", &lv_font_montserrat_20, C_TEXT);
    lv_obj_set_width(s_message_label, 220);
    lv_obj_set_style_text_align(s_message_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_message_label, LV_ALIGN_TOP_MID, 0, 220);

    s_submessage_label = label(s_screen, "UP/DOWN BET · OK SPIN", &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(s_submessage_label, 220);
    lv_obj_set_style_text_align(s_submessage_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_submessage_label, LV_ALIGN_TOP_MID, 0, 247);

    lv_obj_t *hint = label(s_screen, "HOLD UP MAX · HOLD DN SOUND · HOLD OK STATS",
                           &lv_font_montserrat_14, C_MUTED);
    lv_obj_set_width(hint, 220);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);

    s_battery_label = label(s_screen, "BAT --", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(s_battery_label, LV_ALIGN_BOTTOM_RIGHT, -12, -4);

    s_stats_panel = box(s_screen, 22, 70, 196, 192, C_PANEL, C_GOLD, 18);
    lv_obj_set_style_border_width(s_stats_panel, 2, 0);
    s_stats_label = label(s_stats_panel, "", &lv_font_montserrat_14, C_TEXT);
    lv_obj_set_width(s_stats_label, 164);
    lv_obj_set_style_text_align(s_stats_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_stats_label);
    visible(s_stats_panel, false);

    update_header();
    update_stats_panel();
    s_spin_timer = lv_timer_create(spin_timer, 40, NULL);
    lv_screen_load(s_screen);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Neon Jackpot starting");

    store_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL initialization failed");
        return;
    }
    bsp_display_backlight(100);
    (void)bsp_battery_init();

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    s_audio_queue = xQueueCreate(AUDIO_QUEUE_DEPTH, sizeof(sfx_t));
    if (!s_input_queue) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return;
    }

    if (bsp_audio_init() == ESP_OK && s_audio_queue) {
        s_audio_ready = true;
        if (xTaskCreate(audio_task, "slot_audio", 4096, NULL, 4, NULL) != pdPASS)
            s_audio_ready = false;
    }

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "cannot lock LVGL");
        return;
    }
    build_ui();
    bsp_lvgl_unlock();

    if (bsp_button_init(on_button, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "button initialization failed");
        return;
    }

    if (xTaskCreate(input_task, "slot_input", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "input task allocation failed");
        return;
    }

    s_input_ready = true;
    update_battery();
    ESP_LOGI(TAG, "Neon Jackpot ready: credits=%lu bet=%lu sound=%u",
             (unsigned long)s_store.credits,
             (unsigned long)s_store.bet,
             (unsigned)s_store.sound_enabled);
}
