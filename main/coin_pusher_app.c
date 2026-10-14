#include "pusher_physics.h"
#include "coin_assets.h"
#include "coin_save.h"
#include <stddef.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#define APP_FPS_MS 33
#define APP_SAVE_FORMAT 3u
#define APP_V1_MAX_COINS 72u
#define APP_SAVE_SECONDS 5
#define COLOR_BG 0x080D19
#define COLOR_PAPER 0xF8E7BC
#define COLOR_GOLD 0xF6C953

LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk);

static const char *TAG = "coin_pusher";
static cp_game_t s_game;
static cp_game_t s_save_snapshot;
static cp_save_t s_compact_save;
static lv_obj_t *s_screen, *s_stage, *s_cursor;
static lv_obj_t *s_bank, *s_pool, *s_status, *s_battery, *s_pause;
/* Rendering uses one custom LVGL draw callback, not 192 child widgets.
 * This keeps object memory bounded on the ESP32-C3 without PSRAM. */
static lv_timer_t *s_timer;
static lv_font_t s_zh_font;
static bool s_zh_ok, s_nvs_ready, s_dirty;
static volatile int s_battery_level = -1;
static QueueHandle_t s_keys;
static TaskHandle_t s_input_task, s_save_task;
static uint32_t s_render_count;
static uint32_t s_last_collected, s_last_lost, s_last_hits;
static int64_t s_last_frame, s_metrics_start;
static uint32_t s_frames, s_max_step_us;
static const int PIN_Y = 58, TRAY_Y = 126;
static const float TRAY_SCALE = 0.67f;

/* NVS format 1 used the original 72-coin struct. Keep old saves loadable. */
typedef struct {
    cp_coin_t coins[APP_V1_MAX_COINS];
    uint32_t rng;
    uint32_t bank, score, best, jackpot_pool;
    uint32_t drops, collected, lost, jackpot_hits;
    uint8_t combo;
    float drop_x, pusher_y, phase, event_left, combo_left;
    float game_time, event_clock;
    cp_event_t event;
    bool paused;
} cp_legacy_save_v1_t;

static cp_legacy_save_v1_t s_legacy;

typedef struct {
    bsp_btn_t key;
    bsp_btn_ev_t ev;
} key_msg_t;

static lv_obj_t *solid(lv_obj_t *parent, int x, int y, int w, int h,
                       uint32_t fill, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *value, int x, int y,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, value);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    return o;
}

static bool glyphs(const lv_font_t *font, const char *utf8)
{
    while (*utf8) {
        uint32_t cp = 0;
        uint8_t v = (uint8_t)*utf8++;
        if (v < 0x80) cp = v;
        else {
            uint8_t n;
            if ((v & 0xe0) == 0xc0) { cp = v & 0x1f; n = 1; }
            else if ((v & 0xf0) == 0xe0) { cp = v & 0x0f; n = 2; }
            else return false;
            for (uint8_t i = 0; i < n; ++i) {
                uint8_t next = (uint8_t)*utf8++;
                if ((next & 0xc0) != 0x80) return false;
                cp = (cp << 6) | (next & 0x3f);
            }
        }
        if (cp < 0x20) continue;
        lv_font_glyph_dsc_t dsc = {0};
        if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0) || dsc.is_placeholder) {
            return false;
        }
    }
    return true;
}

static const char *event_text(cp_event_t event)
{
    const char *zh;
    switch (event) {
    case CP_EVENT_COIN_RAIN: zh = "金币雨"; break;
    case CP_EVENT_MAGNET: zh = "磁力场"; break;
    case CP_EVENT_EARTHQUAKE: zh = "地震"; break;
    case CP_EVENT_GOLD_RUSH: zh = "黄金时刻"; break;
    case CP_EVENT_MOON: zh = "月球重力"; break;
    case CP_EVENT_PORTAL: zh = "虫洞传送"; break;
    case CP_EVENT_BLACK_HOLE: zh = "黑洞吸引"; break;
    case CP_EVENT_JACKPOT: zh = "超级大奖"; break;
    case CP_EVENT_RAPID_FIRE: return "RAPID FIRE";
    default: zh = "开始推币"; break;
    }
    return s_zh_ok && glyphs(&s_zh_font, zh) ? zh : cp_event_name(event);
}

/* Flash-resident RGB565 assets draw directly into LVGL's existing tiles. */
static void sprite(lv_layer_t *layer, const lv_image_dsc_t *image, int x, int y)
{
    lv_draw_image_dsc_t d;
    lv_draw_image_dsc_init(&d);
    d.src=image;
    lv_area_t a={.x1=x, .y1=y, .x2=x+(int)image->header.w-1,
                 .y2=y+(int)image->header.h-1};
    lv_draw_image(layer, &d, &a);
}

static void draw_scene(lv_event_t *event)
{
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_area_t box; lv_obj_get_coords(s_stage, &box);
    int ox=box.x1, oy=box.y1-54;
    for (unsigned row=0; row<CP_PEG_ROWS; ++row) {
        for (unsigned col=0; col<CP_PEG_COLS; ++col) {
            float x,y; cp_peg_position(row,col,&x,&y);
            sprite(layer,&cp_asset_peg,ox+4+(int)x-2,oy+PIN_Y+(int)y-2);
        }
    }
    for (unsigned i=0;i<CP_MAX_BALLS;++i) {
        const cp_ball_t *b=&s_game.balls[i];
        if (b->active) sprite(layer,&cp_asset_ball,ox+4+(int)b->x-4,
                              oy+PIN_Y+(int)b->y-4);
    }
    if (s_game.slot_flash>0) sprite(layer,&cp_asset_flash,ox+15+(int)(s_game.last_slot*41.6f),oy+112);
    for (unsigned slot=0;slot<5;++slot) {
        lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
        d.bg_color=lv_color_hex(s_game.slot_flash>0 && s_game.last_slot==slot ?
                               0xFFF2A0 : slot==2?0xB02F68:0x976B24);
        d.bg_opa=LV_OPA_COVER;
        lv_area_t a={.x1=ox+5+(int)(slot*41.6f),.y1=oy+123,
                     .x2=ox+43+(int)(slot*41.6f),.y2=oy+126};
        lv_draw_rect(layer,&d,&a);
    }
    sprite(layer,&cp_asset_pusher,ox+4,oy+TRAY_Y+(int)(s_game.pusher_y*TRAY_SCALE)-6);
    static const lv_image_dsc_t *coins[]={&cp_asset_copper,&cp_asset_gold,
        &cp_asset_ruby,&cp_asset_platinum};
    /* Height buckets keep stacking legible without allocating per-coin widgets. */
    for (unsigned elevation=0;elevation<4;++elevation) {
        for (unsigned i=0;i<CP_MAX_COINS;++i) {
            const cp_coin_t *c=&s_game.coins[i];
            if (!c->active) continue;
            unsigned z=c->z<2?0:c->z<5?1:c->z<9?2:3;
            if (z!=elevation) continue;
            const lv_image_dsc_t *im=coins[c->kind];
            int x=ox+4+(int)c->x-(int)im->header.w/2;
            int y=oy+TRAY_Y+(int)(c->y*TRAY_SCALE-c->z*0.4f)-(int)im->header.h/2;
            if (y<oy+130 || y>oy+279) continue;
            sprite(layer,im,x,y);
        }
    }
}

static void make_scene(void)
{
    s_zh_font = lv_font_source_han_sans_sc_16_cjk;
    s_zh_font.fallback = &lv_font_montserrat_14;
    s_zh_ok = glyphs(&s_zh_font, "金币推土机");
    s_screen=solid(NULL,0,0,240,320,COLOR_BG,0);
    lv_obj_t *background=lv_image_create(s_screen);
    lv_image_set_src(background,&cp_asset_background);
    lv_obj_set_pos(background,0,0);
    s_bank=label(s_screen,"120 x1",23,37,&lv_font_montserrat_14,COLOR_PAPER);
    s_pool=label(s_screen,"JP 70",105,37,&lv_font_montserrat_14,COLOR_GOLD);
    s_battery=label(s_screen,"--%",183,37,&lv_font_montserrat_14,0xFFFFFF);
    s_stage=solid(s_screen,12,54,216,226,0,0);
    lv_obj_set_style_bg_opa(s_stage,LV_OPA_TRANSP,0);
    lv_obj_add_event_cb(s_stage,draw_scene,LV_EVENT_DRAW_MAIN_END,NULL);
    s_cursor=solid(s_stage,105,0,8,4,COLOR_GOLD,1);
    s_pause=label(s_stage,"PAUSED",68,99,&lv_font_montserrat_20,0xFFFFFF);
    lv_obj_add_flag(s_pause,LV_OBJ_FLAG_HIDDEN);
    s_status=label(s_screen,"",22,284,&s_zh_font,COLOR_GOLD);
    lv_obj_set_width(s_status,196);
    lv_label_set_long_mode(s_status,LV_LABEL_LONG_CLIP);
    label(s_screen,"<",29,304,&lv_font_montserrat_14,COLOR_PAPER);
    label(s_screen,"OK DROP",86,304,&lv_font_montserrat_14,0xFFFFFF);
    label(s_screen,">",200,304,&lv_font_montserrat_14,COLOR_PAPER);
    lv_screen_load(s_screen);
}

static void render(void)
{
    lv_obj_set_x(s_cursor,4+(int)s_game.drop_x-4);
    lv_obj_invalidate(s_stage);
    if (s_game.paused) lv_obj_remove_flag(s_pause, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_pause, LV_OBJ_FLAG_HIDDEN);

    if ((s_render_count++ % 4) == 0) {
        lv_label_set_text_fmt(s_bank, "%lu x%d", (unsigned long)s_game.bank,
                              s_game.combo ? (int)s_game.combo : 1);
        lv_label_set_text_fmt(s_pool, "JP %lu", (unsigned long)s_game.jackpot_pool);
        const char *message = s_game.paused ? "PAUSED" :
                              s_game.boost_left > 0.0f ? "HYDRAULIC BOOST" :
                              cp_coin_count(&s_game) >= CP_MAX_COINS ? "TRAY FULL" :
                              s_game.bank == 0 ? "DOUBLE OK = +30" :
                              event_text(s_game.event);
        char status[68];
        snprintf(status, sizeof(status), "%s %u/%u", message,
                 cp_coin_count(&s_game), (unsigned)CP_MAX_COINS);
        lv_label_set_text(s_status, status);
    }
    if ((s_render_count % 30) == 0) {
        if (s_battery_level >= 0) lv_label_set_text_fmt(s_battery, "%d%%", s_battery_level);
        else lv_label_set_text(s_battery, "--%");
    }
}

static void on_frame(lv_timer_t *timer)
{
    (void)timer;
    int64_t now=esp_timer_get_time();
    float elapsed=s_last_frame ? (float)(now-s_last_frame)/1000000.0f : 1.0f/30.0f;
    s_last_frame=now;
    if (!s_game.paused) cp_step(&s_game,elapsed);
    uint32_t step_us=(uint32_t)(esp_timer_get_time()-now);
    if (step_us>s_max_step_us) s_max_step_us=step_us;
    s_frames++;
    if (now-s_metrics_start>=10000000) {
        ESP_LOGI(TAG,"SIM %.1f Hz step max %lu us heap %u min %u largest %u coins %u balls %u",
            (double)s_frames*1000000.0/(double)(now-s_metrics_start),
            (unsigned long)s_max_step_us,(unsigned)esp_get_free_heap_size(),
            (unsigned)esp_get_minimum_free_heap_size(),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
            cp_coin_count(&s_game),cp_ball_count(&s_game));
        s_metrics_start=now; s_frames=0; s_max_step_us=0;
    }
    if (s_last_collected != s_game.collected || s_last_lost != s_game.lost ||
        s_last_hits != s_game.jackpot_hits) {
        s_dirty = true;
        s_last_collected = s_game.collected;
        s_last_lost = s_game.lost;
        s_last_hits = s_game.jackpot_hits;
    }
    render();
}

static void on_key(bsp_btn_t button, bsp_btn_ev_t event, void *ctx)
{
    (void)ctx;
    if (!s_keys) return;
    key_msg_t msg = {.key = button, .ev = event};
    (void)xQueueSend(s_keys, &msg, 0);
}

static void input_loop(void *ctx)
{
    (void)ctx;
    key_msg_t msg;
    while (true) {
        if (xQueueReceive(s_keys, &msg, portMAX_DELAY) != pdTRUE) continue;
        if (!bsp_lvgl_lock(200)) continue;
        if (msg.ev == BSP_BTN_LONG && msg.key == BSP_BTN_OK) {
            s_game.paused = !s_game.paused;
            s_dirty = true;
        } else if (!s_game.paused && msg.ev == BSP_BTN_CLICK) {
            if (msg.key == BSP_BTN_UP) cp_aim(&s_game, -10.0f);
            if (msg.key == BSP_BTN_DOWN) cp_aim(&s_game, 10.0f);
            if (msg.key == BSP_BTN_OK) {
                if (cp_drop(&s_game)) s_dirty = true;
                else if (s_game.bank > 0 &&
                         cp_coin_count(&s_game) >= CP_MAX_COINS &&
                         cp_pulse(&s_game)) s_dirty = true;
            }
        } else if (!s_game.paused && msg.ev == BSP_BTN_DOUBLE &&
                   (msg.key == BSP_BTN_UP || msg.key == BSP_BTN_DOWN)) {
            if (cp_pulse(&s_game)) s_dirty = true;
        } else if (!s_game.paused && msg.ev == BSP_BTN_DOUBLE && msg.key == BSP_BTN_OK) {
            if (s_game.bank == 0) {
                s_game.bank = 30;  /* Offline free-play recovery, not a monetary prize. */
                s_dirty = true;
            } else if (cp_buy_chaos(&s_game)) s_dirty = true;
        }
        bsp_lvgl_unlock();
    }
}

static bool saved_game_valid(const cp_game_t *g)
{
    if (g->rng == 0 || g->bank > 10000000u || g->jackpot_pool > 10000000u ||
        g->event > CP_EVENT_RAPID_FIRE || !isfinite(g->game_time) ||
        !isfinite(g->event_left) || !isfinite(g->boost_left) ||
        !isfinite(g->phase) || !isfinite(g->pusher_y) || !isfinite(g->drop_x)) {
        return false;
    }
    for (unsigned i = 0; i < CP_MAX_COINS; ++i) {
        const cp_coin_t *c = &g->coins[i];
        if (!c->active) continue;
        if (c->kind > CP_COIN_GIANT ||
            !isfinite(c->x) || !isfinite(c->y) || !isfinite(c->z) ||
            !isfinite(c->vx) || !isfinite(c->vy) || !isfinite(c->vz) ||
            !isfinite(c->radius) || !isfinite(c->height) || !isfinite(c->mass) ||
            c->mass<=0 || c->height<=0 || c->height>10 || c->radius < 3.0f || c->radius > 15.0f) return false;
    }
    for (unsigned i=0;i<CP_MAX_BALLS;++i) {
        const cp_ball_t *b=&g->balls[i];
        if (b->active && (b->kind>CP_COIN_GIANT || !isfinite(b->x) ||
            !isfinite(b->y) || !isfinite(b->vx) || !isfinite(b->vy))) return false;
    }
    return cp_coin_count(g)+cp_ball_count(g)<=CP_MAX_COINS;
}

static void read_save(void)
{
    uint32_t format = 0;
    nvs_handle_t h;
    if (!s_nvs_ready || nvs_open("coin_pusher", NVS_READONLY, &h) != ESP_OK) return;
    size_t bytes = sizeof(s_save_snapshot);
    if (nvs_get_u32(h, "format", &format) != ESP_OK) {
        nvs_close(h);
        return;
    }
    bytes=sizeof(s_compact_save);
    if (format == APP_SAVE_FORMAT &&
        nvs_get_blob(h,"game",&s_compact_save,&bytes)==ESP_OK &&
        bytes==sizeof(s_compact_save) &&
        cp_save_decode(&s_save_snapshot,&s_compact_save) &&
        saved_game_valid(&s_save_snapshot)) {
        memcpy(&s_game, &s_save_snapshot, sizeof(s_game));
        s_game.paused = false;
        ESP_LOGI(TAG, "Saved tray and virtual credits restored");
    } else if (format == 2u) {
        memset(&s_save_snapshot,0,sizeof(s_save_snapshot));
        bytes=offsetof(cp_game_t,balls);
        if (nvs_get_blob(h,"game",&s_save_snapshot,&bytes)==ESP_OK &&
            bytes==offsetof(cp_game_t,balls) && saved_game_valid(&s_save_snapshot)) {
            memcpy(&s_game,&s_save_snapshot,sizeof(s_game));
            s_game.paused=false; s_dirty=true;
            ESP_LOGI(TAG,"Migrated v2 tray; pegboard starts empty");
        }
    } else if (format == 1u) {
        bytes = sizeof(s_legacy);
        if (nvs_get_blob(h, "game", &s_legacy, &bytes) == ESP_OK &&
            bytes == sizeof(s_legacy) && s_legacy.rng != 0u &&
            s_legacy.bank <= 10000000u) {
            cp_init(&s_game, s_legacy.rng, s_legacy.bank,
                    s_legacy.best, s_legacy.jackpot_pool);
            memset(s_game.coins,0,sizeof(s_game.coins));
            memcpy(s_game.coins, s_legacy.coins, sizeof(s_legacy.coins));
            s_game.rng = s_legacy.rng;
            s_game.score = s_legacy.score;
            s_game.drops = s_legacy.drops;
            s_game.collected = s_legacy.collected;
            s_game.lost = s_legacy.lost;
            s_game.jackpot_hits = s_legacy.jackpot_hits;
            s_game.combo = s_legacy.combo;
            s_game.drop_x = s_legacy.drop_x;
            s_game.pusher_y = s_legacy.pusher_y;
            s_game.phase = s_legacy.phase;
            s_game.event_left = s_legacy.event_left;
            s_game.combo_left = s_legacy.combo_left;
            s_game.game_time = s_legacy.game_time;
            s_game.event_clock = s_legacy.event_clock;
            s_game.event = s_legacy.event;
            s_game.last_progress = s_game.game_time;
            s_game.paused = false;
            if (!saved_game_valid(&s_game)) cp_init(&s_game,esp_random(),120,0,70);
            s_dirty = true;
            ESP_LOGI(TAG, "Migrated coin pusher save from 72-coin format");
        }
    }
    nvs_close(h);
}

static void save_loop(void *ctx)
{
    (void)ctx;
    unsigned saves = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(APP_SAVE_SECONDS * 1000));
        s_battery_level = bsp_battery_soc();
        if (!s_nvs_ready || !bsp_lvgl_lock(2000)) continue;
        /* Save currency changes promptly and drifting tray state every 30 s. */
        bool must_save = s_dirty || (++saves % 6u) == 0u;
        if (must_save) {
            memcpy(&s_save_snapshot, &s_game, sizeof(s_game));
            s_dirty = false;
        }
        bsp_lvgl_unlock();
        if (!must_save) continue;
        cp_save_encode(&s_compact_save,&s_save_snapshot);
        nvs_handle_t h;
        esp_err_t err = nvs_open("coin_pusher", NVS_READWRITE, &h);
        if (err == ESP_OK) {
            err = nvs_set_u32(h, "format", APP_SAVE_FORMAT);
            if (err == ESP_OK)
                err = nvs_set_blob(h, "game", &s_compact_save, sizeof(s_compact_save));
            if (err == ESP_OK) err = nvs_commit(h);
            nvs_close(h);
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "NVS save failed: %s", esp_err_to_name(err));
            if (bsp_lvgl_lock(200)) {
                s_dirty = true;
                bsp_lvgl_unlock();
            }
        }
    }
}

void coin_pusher_start(void)
{
    esp_err_t nvs = nvs_flash_init();
    s_nvs_ready = nvs == ESP_OK;
    if (!s_nvs_ready) ESP_LOGW(TAG, "NVS unavailable, running without persistence");

    cp_init(&s_game, esp_random(), 120u, 0u, 70u);
    read_save();
    s_last_collected = s_game.collected;
    s_last_lost = s_game.lost;
    s_last_hits = s_game.jackpot_hits;
    bsp_i2c_init();
    (void)bsp_battery_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }
    bsp_display_backlight(85);
    s_keys = xQueueCreate(16, sizeof(key_msg_t));
    if (!s_keys) {
        ESP_LOGE(TAG, "No memory for input queue");
        return;
    }
    if (bsp_lvgl_lock(1000)) {
        make_scene();
        render();
        s_metrics_start=esp_timer_get_time();
        s_timer = lv_timer_create(on_frame, APP_FPS_MS, NULL);
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "LVGL UI lock failed");
        vQueueDelete(s_keys);
        s_keys = NULL;
        return;
    }
    if (xTaskCreate(input_loop, "coin_keys", 3072, NULL, 6, &s_input_task) != pdPASS) {
        ESP_LOGE(TAG, "Input task failed");
        vQueueDelete(s_keys);
        s_keys = NULL;
        return;
    }
    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed");
        vTaskDelete(s_input_task);
        s_input_task = NULL;
        vQueueDelete(s_keys);
        s_keys = NULL;
        return;
    }
    if (xTaskCreate(save_loop, "coin_save", 4096, NULL, 3, &s_save_task) != pdPASS) {
        ESP_LOGW(TAG, "Save task could not start; progress will not persist");
    }
    ESP_LOGI(TAG, "Coin Pusher 2.5D started; coins=%u, fps target=30",
             cp_coin_count(&s_game));
}
