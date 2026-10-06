// Odds Arcade: zero-stakes probability toys for FoloToy AI Passport.
// Four endlessly replayable mini games: slots, roulette, dice and Plinko.
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "chance_core.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#if CONFIG_ARCADE_USB_TEST
#include "driver/usb_serial_jtag.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "light_core.h"
#include "motion_core.h"
#include "plinko_core.h"
#include "slot_symbols.h"
#include "multi_slot_core.h"
#include "arcade_font_inventory.h"
#include <string.h>
LV_FONT_DECLARE(arcade_font_14);
LV_FONT_DECLARE(arcade_font_20);
#include "rigid_body.h"
#include "slot_core.h"
#include "tiny3d_render.h"

#define INPUT_QUEUE_DEPTH 8
#define AUDIO_QUEUE_DEPTH 16
#define LAMP_COUNT 16
#define PLINKO_ROWS 8
#define PLINKO_PEGS 36
#define SETTINGS_ROWS 7
#define MULTI_STRIP_LENGTH MULTI_SLOT_STRIP_LENGTH

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
    PAGE_SETTINGS,
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
    lv_obj_t *image;
    uint8_t shown;
    bool stopped;
} reel_view_t;

typedef struct {
    motion_slot_profile_t profile;
    uint32_t phase_q8;
    uint32_t start_q8, end_q8;
} reel_motion_t;

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
static const char *const SPEED_NAMES[] = { "慢速", "标准", "快速" };

static QueueHandle_t s_input_queue;
static QueueHandle_t s_audio_queue;
static atomic_bool s_audio_ready;
static atomic_bool s_sound = true;
static atomic_bool s_input_ready;
static atomic_uint s_audio_writes;
static atomic_uint s_audio_errors;

static page_t s_page = PAGE_HOME;
static uint8_t s_home_index;
static uint8_t s_speed = 1;
static bool s_auto;
static bool s_busy;
static uint32_t s_started;
static uint32_t s_next_auto;
static uint32_t s_flash_until;
static uint32_t s_completed;
static bool s_fps_sampling;
static uint32_t s_fps_frames;
static uint32_t s_fps_active_ms;
static uint32_t s_fps_last_frame_ms;
static int64_t s_fps_start_us;
static page_t s_fps_page;
static bool s_fps_display_enabled;
static uint32_t s_live_fps_frames;
static uint32_t s_live_fps_active_ms;
static uint32_t s_live_fps_last_frame_ms;
static uint32_t s_live_fps_window_start_ms;

static lv_obj_t *s_screen;
static lv_obj_t *s_title;
static lv_obj_t *s_caption;
static lv_obj_t *s_status;
static lv_obj_t *s_hint;
static lv_obj_t *s_battery;
static lv_obj_t *s_accent_left;
static lv_obj_t *s_accent_right;
static lv_obj_t *s_game_frame;
static lv_obj_t *s_fps_badge;
static lv_timer_t *s_anim_timer;

static bool is_game_page(page_t page)
{
    return page >= PAGE_SLOT && page <= PAGE_PLINKO;
}

static const char *fps_page_name(page_t page)
{
    switch (page) {
    case PAGE_SLOT: return "slots";
    case PAGE_ROULETTE: return "roulette";
    case PAGE_DICE: return "dice";
    case PAGE_PLINKO: return "plinko";
    case PAGE_HOME:
    default: return "home";
    }
}

static void fps_refresh_event_cb(lv_event_t *event)
{
    (void)event;
    uint32_t now = lv_tick_get();

    if (s_fps_sampling) {
        if (s_page != s_fps_page || !s_busy) {
            s_fps_last_frame_ms = 0;
        } else {
            if (s_fps_last_frame_ms != 0) {
                s_fps_active_ms += now - s_fps_last_frame_ms;
            }
            s_fps_last_frame_ms = now;
            ++s_fps_frames;
        }
    }

    if (!s_fps_display_enabled || !s_fps_badge) return;
    if (!is_game_page(s_page) || !s_busy) {
        s_live_fps_frames = 0;
        s_live_fps_active_ms = 0;
        s_live_fps_last_frame_ms = 0;
        s_live_fps_window_start_ms = now;
        if (lv_label_get_text(s_fps_badge)[0] != '-')
            lv_label_set_text(s_fps_badge, "-- 帧");
        return;
    }

    if (s_live_fps_last_frame_ms != 0) {
        s_live_fps_active_ms += now - s_live_fps_last_frame_ms;
    }
    s_live_fps_last_frame_ms = now;
    ++s_live_fps_frames;
    if (s_live_fps_window_start_ms == 0) s_live_fps_window_start_ms = now;
    if (now - s_live_fps_window_start_ms >= 1000) {
        uint32_t fps_x100 = s_live_fps_active_ms && s_live_fps_frames > 1
            ? (uint32_t)(((uint64_t)(s_live_fps_frames - 1) * 100000u) /
                         s_live_fps_active_ms)
            : 0;
        lv_label_set_text_fmt(s_fps_badge, "%lu 帧",
                              (unsigned long)((fps_x100 + 50) / 100));
        s_live_fps_frames = 0;
        s_live_fps_active_ms = 0;
        s_live_fps_last_frame_ms = now;
        s_live_fps_window_start_ms = now;
    }
}

/* slot */
static reel_view_t s_reels[3];
static reel_view_t s_reel_rows[3][4];
static lv_obj_t *s_neon[34];
static uint8_t s_neon_opacity[34];
static uint8_t s_neon_mode;
static uint8_t s_settings_index;
static uint8_t s_dice_count=2;
static uint32_t s_neon_tick;
static plinko_ball_t s_plinko_physics[PLINKO_MAX_BALLS];
static uint8_t s_plinko_count=1;
static int64_t s_plinko_last_us,s_plinko_accum_us;
static int32_t s_wheel_start,s_wheel_end,s_ball_start,s_ball_end;
static slot_result_t s_slot_result;
static reel_motion_t s_reel_motion[3];
static uint8_t s_slot_mode;
static uint32_t s_slot_last_score[2], s_slot_total[2];
static reel_view_t s_multi_rows[5][5];
static reel_motion_t s_multi_motion[5];
static uint8_t s_multi_strip[5][MULTI_STRIP_LENGTH];
static multi_slot_result_t s_multi_result;
static lv_obj_t *s_multi_highlights[MULTI_SLOT_CELLS];
static lv_obj_t *s_multi_lines[MULTI_SLOT_LINES];
static lv_point_precise_t s_multi_points[MULTI_SLOT_LINES][4];

/* roulette */
static lv_obj_t *s_wheel_lamps[LAMP_COUNT];
static lv_obj_t *s_roulette_wheel;
static lv_obj_t *s_roulette_glint;
static lv_obj_t *s_roulette_number;
static lv_obj_t *s_roulette_color;
static chance_roulette_result_t s_roulette_result;
static uint8_t s_roulette_pos;
static roulette_motion_t s_roulette_motion;

/* dice: fixed-point rigid bodies + software RGB565 renderer */
static lv_obj_t *s_dice_canvas;
static uint16_t s_dice_canvas_pixels[T3D_DICE_CANVAS_W * T3D_DICE_CANVAS_H]
    __attribute__((aligned(4)));
static t3d_surface_t s_dice_surface = {
    .pixels = s_dice_canvas_pixels,
    .width = T3D_DICE_CANVAS_W,
    .height = T3D_DICE_CANVAS_H,
    .stride = T3D_DICE_CANVAS_W,
};
static rigid_die_t s_rigid_dice[6];
static uint8_t s_die_result[6];
static int64_t s_dice_last_us;
static int64_t s_dice_accum_us;
static uint32_t s_dice_deadline;

/* plinko */
static lv_obj_t *s_plinko_shadow;
static lv_obj_t *s_plinko_ball[PLINKO_MAX_BALLS];
static lv_obj_t *s_plinko_highlight[PLINKO_MAX_BALLS];
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
        esp_err_t error = bsp_audio_write(pcm, n * sizeof(pcm[0]));
        if (error != ESP_OK) {
            ++s_audio_errors;
            ESP_LOGE(TAG, "audio write failed: %s", esp_err_to_name(error));
            return;
        }
        ++s_audio_writes;
        total -= n;
    }
}

static void audio_task(void *arg)
{
    (void)arg;
    if (bsp_audio_set_format(16000, 16, 1) != ESP_OK) {
        s_audio_ready = false;
        ++s_audio_errors;
        ESP_LOGE(TAG, "audio format setup failed");
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
    s_caption = NULL;
    s_status = NULL;
    s_hint = NULL;
    s_battery = NULL;
    s_accent_left = NULL;
    s_accent_right = NULL;
    s_game_frame = NULL;
    memset(s_multi_highlights,0,sizeof(s_multi_highlights));
    memset(s_multi_lines,0,sizeof(s_multi_lines));
    s_fps_badge = NULL;
    s_live_fps_frames = 0;
    s_live_fps_active_ms = 0;
    s_live_fps_last_frame_ms = 0;
    s_live_fps_window_start_ms = 0;
    for (int i = 0; i < 3; ++i) {
        s_reels[i].cell = NULL;
        s_reels[i].image = NULL;
    }
    for (int i = 0; i < LAMP_COUNT; ++i) s_wheel_lamps[i] = NULL;
    s_roulette_wheel = NULL;
    s_roulette_glint = NULL;
    s_roulette_number = NULL;
    s_roulette_color = NULL;
    s_dice_canvas = NULL;
    s_plinko_shadow = NULL;
    memset(s_plinko_ball,0,sizeof(s_plinko_ball));
    memset(s_plinko_highlight,0,sizeof(s_plinko_highlight));
    for (int i = 0; i <= PLINKO_ROWS; ++i) s_plinko_bins[i] = NULL;
}

static void update_battery_unlocked(void)
{
    if (!s_battery) return;
    int soc = bsp_battery_soc();
    if (soc < 0) lv_label_set_text(s_battery, "电量 --");
    else lv_label_set_text_fmt(s_battery, "电量 %d%%", soc);
}

static void build_neon(void)
{
    static const uint32_t colors[]={C_MAGENTA,C_CYAN,C_GOLD,0x886BFF,C_GREEN,C_RED};
    for(int i=0;i<34;i++) {
        int x,y,w,h;
        if(i<7){x=5+i*33;y=3;w=30;h=3;}
        else if(i<17){x=234;y=8+(i-7)*30;w=3;h=27;}
        else if(i<24){x=203-(i-17)*33;y=314;w=30;h=3;}
        else{x=3;y=278-(i-24)*30;w=3;h=27;}
        uint32_t c=colors[(i*6/34)%6];
        s_neon[i]=box(s_screen,x,y,w,h,c,c,2);
        s_neon_opacity[i]=0;
    }
    s_neon_tick=0;
}
static void animate_neon(uint32_t now)
{
    if(now-s_neon_tick<100)return;
    s_neon_tick=now;
    int head=(now/100)%34;
    for(int i=0;i<34;i++) {
        int distance=(head-i+34)%34;
        uint8_t opacity;
        if(s_neon_mode==1){int t=(now/12+i*30)%510;opacity=60+(t<255?t:510-t)*195/255;}
        else if(s_neon_mode==2){int a=(head-i+34)%34;int b=(i+head+17)%34;int d=a<b?a:b;opacity=d<5?255-d*35:55;}
        else opacity=distance<6?255-distance*33:50;
        if(opacity!=s_neon_opacity[i]){lv_obj_set_style_bg_opa(s_neon[i],opacity,0);s_neon_opacity[i]=opacity;}
    }
}
static void build_shell(const char *title_text)
{
    clear_page_refs();lv_obj_clean(s_screen);
    lv_obj_set_style_bg_color(s_screen,lv_color_hex(C_BG),0);
    build_neon();
    s_title=label(s_screen,title_text,&arcade_font_20,C_GOLD);
    lv_obj_set_pos(s_title,14,14);
    s_battery=label(s_screen,"电量 --",&arcade_font_14,C_MUTED);
    lv_obj_align(s_battery,LV_ALIGN_TOP_RIGHT,-14,18);update_battery_unlocked();
    s_caption=label(s_screen,"霓虹游乐场",&arcade_font_14,C_MUTED);
    lv_obj_set_pos(s_caption,14,43);
    if(s_fps_display_enabled&&is_game_page(s_page)) {
        lv_obj_t *badge=box(s_screen,166,40,58,22,C_PANEL,C_CYAN,6);
        s_fps_badge=label(badge,"-- 帧",&arcade_font_14,C_CYAN);lv_obj_center(s_fps_badge);
    }
}
static void text_line(lv_obj_t *obj,int y,int height)
{
    lv_obj_set_pos(obj,12,y);lv_obj_set_size(obj,216,height);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(obj,LV_TEXT_ALIGN_CENTER,0);
}
static void set_game_footer(const char *status_text)
{
    s_status=label(s_screen,status_text,&arcade_font_20,C_TEXT);text_line(s_status,247,26);
    s_hint=label(s_screen,"",&arcade_font_14,C_MUTED);text_line(s_hint,275,17);
    lv_obj_t *home=label(s_screen,s_page==PAGE_DICE?"长按上自动  长按确认返回":"长按上自动  长按确认返回",&arcade_font_14,C_MUTED);
    text_line(home,294,20);
}
static void refresh_hint(void)
{
    if(!s_hint||!is_game_page(s_page))return;
    if(s_page==PAGE_SLOT) {
        if(s_busy)lv_label_set_text_fmt(s_hint,"本局 --  累计 %lu",(unsigned long)s_slot_total[s_slot_mode]);
        else lv_label_set_text_fmt(s_hint,"本局 %lu 分  累计 %lu",(unsigned long)s_slot_last_score[s_slot_mode],(unsigned long)s_slot_total[s_slot_mode]);
        lv_label_set_text_fmt(s_caption,"%s %s",SPEED_NAMES[s_speed],s_sound?"有声":"静音");
    }
    else if(s_page==PAGE_PLINKO)lv_label_set_text_fmt(s_hint,"上键%u球  %s  %s",s_plinko_count,s_auto?"自动":"手动",s_sound?"有声":"静音");
    else if(s_page==PAGE_DICE)lv_label_set_text_fmt(s_hint,"上键%u枚  %s  %s",s_dice_count,s_auto?"自动":"手动",s_sound?"有声":"静音");
    else lv_label_set_text_fmt(s_hint,"%s  %s  %s",SPEED_NAMES[s_speed],s_auto?"自动":"手动",s_sound?"有声":"静音");
}

static void flash_machine(uint32_t ms)
{
    s_flash_until = lv_tick_get() + ms;
}

static void render_symbol(reel_view_t *reel, uint8_t symbol)
{
    if (!reel->image || reel->shown == symbol) return;
    if (symbol >= SLOT_SYMBOL_COUNT) return;
    reel->shown = symbol;
    lv_image_set_src(reel->image, &ARCADE_SLOT_SYMBOLS[symbol]);
}

static void reel_layout(int index,uint32_t position)
{
    int symbol=(int)(position>>8),fraction=(int)(position&255);
    for(int row=0;row<4;row++) {
        int j=row-2;
        reel_view_t *view=&s_reel_rows[index][row];
        render_symbol(view,(uint8_t)((symbol-j+SLOT_SYMBOL_COUNT*10)%SLOT_SYMBOL_COUNT));
        lv_obj_set_y(view->cell,24+j*84+fraction*84/256);
    }
}
static void build_reel(reel_view_t *reel,int x,uint8_t initial)
{
    int index=(int)(reel-s_reels);
    reel->cell=box(s_game_frame,x,14,58,144,C_REEL,C_GOLD_DIM,8);
    lv_obj_set_style_border_width(reel->cell,2,0);
    for(int row=0;row<4;row++) {
        reel_view_t *v=&s_reel_rows[index][row];
        *v=(reel_view_t){0};
        v->cell=box(reel->cell,0,0,54,84,C_REEL,C_REEL,0);
        lv_obj_set_style_border_width(v->cell,0,0);
        v->image=lv_image_create(v->cell);
        lv_obj_set_pos(v->image,3,10);
        v->shown=255;
    }
    reel->stopped=true;s_reel_motion[index].phase_q8=(uint32_t)initial*256;
    reel_layout(index,s_reel_motion[index].phase_q8);
}
static void multi_reel_layout(int column,uint32_t phase)
{
    int index=(int)(phase>>8),fraction=(int)(phase&255);
    for(int row=0;row<5;row++) {
        int j=row-2;
        reel_view_t *v=&s_multi_rows[column][row];
        uint8_t symbol=s_multi_strip[column][(index-j+MULTI_STRIP_LENGTH)%MULTI_STRIP_LENGTH];
        if(v->shown!=symbol){v->shown=symbol;lv_image_set_src(v->image,&ARCADE_SLOT_SMALL_SYMBOLS[symbol]);}
        lv_obj_set_y(v->cell,j*42+fraction*42/256);
    }
}
static void multi_show_wins(bool show)
{
    for(int i=0;i<MULTI_SLOT_CELLS;i++) {
        if(show&&(s_multi_result.cell_mask&(1u<<i)))lv_obj_remove_flag(s_multi_highlights[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_multi_highlights[i],LV_OBJ_FLAG_HIDDEN);
    }
    for(int i=0;i<MULTI_SLOT_LINES;i++) {
        if(show&&(s_multi_result.line_mask&(1u<<i)))lv_obj_remove_flag(s_multi_lines[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_multi_lines[i],LV_OBJ_FLAG_HIDDEN);
    }
}
static void build_multi_slot(void)
{
    for(int c=0;c<5;c++) {
        lv_obj_t *window=box(s_game_frame,8+c*40,12,38,130,C_REEL,C_GOLD_DIM,5);
        lv_obj_set_style_border_width(window,2,0);
        for(int i=0;i<MULTI_STRIP_LENGTH;i++)s_multi_strip[c][i]=(uint8_t)((i+c)%SLOT_SYMBOL_COUNT);
        for(int row=0;row<5;row++) {
            reel_view_t *v=&s_multi_rows[c][row];*v=(reel_view_t){0};
            v->cell=box(window,0,0,34,42,C_REEL,C_REEL,0);lv_obj_set_style_border_width(v->cell,0,0);
            v->image=lv_image_create(v->cell);lv_obj_set_pos(v->image,1,3);v->shown=255;
        }
        s_multi_motion[c].phase_q8=2*256;multi_reel_layout(c,2*256);
    }
    for(int cell=0;cell<15;cell++) {
        s_multi_highlights[cell]=box(s_game_frame,10+(cell%5)*40,14+(cell/5)*42,34,42,C_REEL,C_MAGENTA,4);
        lv_obj_set_style_bg_opa(s_multi_highlights[cell],LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(s_multi_highlights[cell],2,0);
    }
    for(uint8_t line=0;line<MULTI_SLOT_LINES;line++) {
        uint8_t cells[3];multi_slot_line_cells(line,cells);
        for(int i=0;i<3;i++)s_multi_points[line][i]=(lv_point_precise_t){27+(cells[i]%5)*40,35+(cells[i]/5)*42};
        s_multi_points[line][3]=s_multi_points[line][0];
        s_multi_lines[line]=lv_line_create(s_game_frame);
        lv_line_set_points(s_multi_lines[line],s_multi_points[line],line<15?3:4);
        lv_obj_set_style_line_width(s_multi_lines[line],2,0);
        lv_obj_set_style_line_color(s_multi_lines[line],lv_color_hex(line<9?C_MAGENTA:line<15?C_CYAN:C_GOLD),0);
        lv_obj_set_style_line_opa(s_multi_lines[line],170,0);
        lv_obj_set_pos(s_multi_lines[line],0,0);
    }
    multi_show_wins(false);
    lv_obj_t *rules=label(s_game_frame,"横线  斜线  三角",&arcade_font_14,C_MUTED);
    lv_obj_align(rules,LV_ALIGN_BOTTOM_MID,0,-7);
}
static void build_slot(void)
{
    build_shell(s_slot_mode?"多线老虎机":"经典老虎机");
    s_game_frame=box(s_screen,14,68,212,174,C_PANEL_2,C_GOLD,12);
    if(s_slot_mode)build_multi_slot();
    else {
        build_reel(&s_reels[0],9,SLOT_CHERRY);build_reel(&s_reels[1],77,SLOT_BAR);build_reel(&s_reels[2],145,SLOT_SEVEN);
        box(s_game_frame,3,82,206,2,C_MAGENTA,C_MAGENTA,0);
    }
    set_game_footer(s_slot_mode?"三同图连线得分":"确认开始转动");refresh_hint();
}
static void make_canvas(void)
{
    s_dice_canvas=lv_canvas_create(s_game_frame);
    lv_canvas_set_buffer(s_dice_canvas,s_dice_canvas_pixels,T3D_DICE_CANVAS_W,T3D_DICE_CANVAS_H,LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_dice_canvas,4,2);
}
static void build_roulette(void)
{
    build_shell("仿真轮盘");
    s_game_frame=box(s_screen,14,68,212,174,C_PANEL_2,C_GOLD,12);
    make_canvas();t3d_render_roulette(&s_dice_surface,0,48,85);lv_obj_invalidate(s_dice_canvas);
    set_game_footer("确认旋转轮盘");refresh_hint();
}
static void dice_place_initial(bool animate)
{
    for(int i=0;i<s_dice_count;i++) {
        rigid_die_init(&s_rigid_dice[i],esp_random(),(uint8_t)i,s_speed);
        int columns=s_dice_count<=2?s_dice_count:3;
        int rows=(s_dice_count+columns-1)/columns;
        s_rigid_dice[i].pos_q8.x=((i%columns)*48-(columns-1)*24)*256;
        s_rigid_dice[i].pos_q8.z=((i/columns)*50-(rows-1)*25)*256;
        if(s_dice_count>2){s_rigid_dice[i].vel_q8.x/=2;s_rigid_dice[i].vel_q8.z/=2;}
        if(!animate){s_rigid_dice[i].orientation=t3_quat_from_seed((uint32_t)i*992+111);rigid_die_settle(&s_rigid_dice[i]);}
    }
}
static void build_dice(void)
{
    build_shell("俯视骰子");s_game_frame=box(s_screen,14,68,212,174,C_PANEL_2,C_CYAN,12);
    make_canvas();dice_place_initial(false);
    t3d_render_dice_count(&s_dice_surface,s_rigid_dice,s_dice_count);lv_obj_invalidate(s_dice_canvas);
    set_game_footer("确认掷骰子");refresh_hint();
}
static void build_plinko(void)
{
    build_shell("弹珠落盘");s_game_frame=box(s_screen,14,68,212,174,C_PANEL_2,C_CYAN,12);
    for(int row=0;row<PLINKO_CORE_ROWS;row++)for(int col=0;col<plinko_peg_count(row);col++){
        int x,y;plinko_peg_position(row,col,&x,&y);
        box(s_game_frame,x-3,y-3,6,6,0xDBD5B9,C_GOLD,LV_RADIUS_CIRCLE);
    }
    for(int i=0;i<9;i++) {
        int x=14+i*20;s_plinko_bins[i]=box(s_game_frame,x,150,19,20,C_PANEL,C_GOLD_DIM,2);
        lv_obj_t *n=label(s_plinko_bins[i],"",&lv_font_montserrat_14,C_GOLD);lv_label_set_text_fmt(n,"%d",i+1);lv_obj_center(n);
    }
    static const uint32_t colors[]={C_MAGENTA,C_CYAN,C_GOLD,0x70EF95,0xFF9966};
    int size=s_plinko_count==1?10:7;
    for(int i=0;i<s_plinko_count;i++) {
        int x=104+(2*i-(s_plinko_count-1))*6-size/2;
        s_plinko_ball[i]=box(s_game_frame,x,5-size/2,size,size,colors[i%5],C_TEXT,LV_RADIUS_CIRCLE);
        lv_obj_set_style_border_width(s_plinko_ball[i],1,0);
        s_plinko_highlight[i]=box(s_game_frame,x+1,6-size/2,2,2,C_TEXT,C_TEXT,LV_RADIUS_CIRCLE);
    }
    set_game_footer("确认释放弹珠");refresh_hint();
}
static void home_card(int index,int x,int y,const char *name,const char *tag)
{
    bool selected=index==s_home_index;
    lv_obj_t *card=box(s_screen,x,y,100,76,selected?C_PANEL_2:C_PANEL,selected?C_CYAN:0x29314F,10);
    lv_obj_set_style_border_width(card,selected?2:1,0);
    lv_obj_t *n=label(card,name,&arcade_font_20,selected?C_GOLD:C_TEXT);lv_obj_align(n,LV_ALIGN_TOP_MID,0,12);
    lv_obj_t *t=label(card,tag,&arcade_font_14,C_MUTED);lv_obj_align(t,LV_ALIGN_BOTTOM_MID,0,-12);
}
static void build_home(void)
{
    build_shell("霓虹游乐场");
    home_card(0,14,74,"老虎机",s_slot_mode?"五列三行":"三轮卷带");home_card(1,126,74,"轮盘","红黑落格");
    home_card(2,14,160,"骰子","一至六枚");home_card(3,126,160,"弹珠","碰钉落盘");
    s_status=label(s_screen,"上/下选择  确认进入",&arcade_font_14,C_GOLD);text_line(s_status,252,20);
    lv_obj_t *settings=label(s_screen,"长按确认进入设置",&arcade_font_14,C_MUTED);text_line(settings,276,17);
    s_hint=label(s_screen,s_sound?"声音开启":"声音关闭",&arcade_font_14,C_MUTED);text_line(s_hint,294,20);
}
static void build_settings(void)
{
    static const char *names[]={"帧率显示","骰子枚数","霓虹光效","声音","游戏速度","老虎机模式","弹珠球数"};
    static const char *modes[]={"彩虹流动","彩虹呼吸","双向追光"};
    build_shell("游戏设置");
    for(int i=0;i<SETTINGS_ROWS;i++) {
        lv_obj_t *row=box(s_screen,14,69+i*28,212,25,i==s_settings_index?C_PANEL_2:C_PANEL,i==s_settings_index?C_CYAN:C_PANEL,6);
        lv_obj_t *n=label(row,names[i],&arcade_font_14,C_TEXT);lv_obj_set_pos(n,10,4);
        char text[32];
        if(i==0)snprintf(text,sizeof(text),"%s",s_fps_display_enabled?"开启":"关闭");
        else if(i==1)snprintf(text,sizeof(text),"%u 枚",s_dice_count);
        else if(i==2)snprintf(text,sizeof(text),"%s",modes[s_neon_mode]);
        else if(i==3)snprintf(text,sizeof(text),"%s",s_sound?"开启":"关闭");
        else if(i==4)snprintf(text,sizeof(text),"%s",SPEED_NAMES[s_speed]);
        else if(i==5)snprintf(text,sizeof(text),"%s",s_slot_mode?"多线五列":"经典三轮");
        else snprintf(text,sizeof(text),"%u 球",s_plinko_count);
        lv_obj_t *v=label(row,text,&arcade_font_14,i==s_settings_index?C_GOLD:C_MUTED);lv_obj_align(v,LV_ALIGN_RIGHT_MID,-10,0);
    }
    lv_obj_t *controls=label(s_screen,"上/下选择  确认修改",&arcade_font_14,C_GOLD);text_line(controls,273,20);
    lv_obj_t *back=label(s_screen,"长按确认返回",&arcade_font_14,C_MUTED);text_line(back,294,20);
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
    case PAGE_SETTINGS: build_settings(); break;
    case PAGE_HOME:
    default: build_home(); break;
    }
}

static void finish_action(const char *message, bool special)
{
    s_busy = false;
    ++s_completed;
    ESP_LOGI(TAG, "result page=%d count=%lu %s", s_page, (unsigned long)s_completed, message);
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
        if(s_page==PAGE_SLOT&&s_slot_mode&&s_slot_last_score[1]) {
            base=speed_duration(1400,1000,700);jitter=180;
        }
        s_next_auto = lv_tick_get() + base + (esp_random() % jitter);
    }
}

static void finish_slot_round(void)
{
    uint32_t score=s_slot_mode?s_multi_result.score:slot_classic_score(s_slot_result.reels);
    s_slot_last_score[s_slot_mode]=score;
    if(UINT32_MAX-s_slot_total[s_slot_mode]<score)s_slot_total[s_slot_mode]=UINT32_MAX;
    else s_slot_total[s_slot_mode]+=score;
    char cells[16]={0},message[48];
    unsigned count=s_slot_mode?15:3;
    for(unsigned i=0;i<count;i++)cells[i]=(char)('0'+(s_slot_mode?s_multi_result.cells[i]:s_slot_result.reels[i]));
    unsigned lines=s_slot_mode?s_multi_result.line_count:chance_is_triple(s_slot_result.reels);
    unsigned pairs=s_slot_mode?s_multi_result.pair_count:(!lines&&chance_is_pair(s_slot_result.reels));
    ESP_LOGI(TAG,"slot_score mode=%u cells=%s score=%lu total=%lu lines=%u pairs=%u",s_slot_mode,cells,(unsigned long)score,(unsigned long)s_slot_total[s_slot_mode],lines,pairs);
    if(s_slot_mode) {
        multi_show_wins(true);
        if(lines)snprintf(message,sizeof(message),"中奖 %u 线",lines);
        else if(pairs)snprintf(message,sizeof(message),"两连小奖");
        else snprintf(message,sizeof(message),"下局再试");
    } else snprintf(message,sizeof(message),"%s",lines?"三连中奖！":pairs?"两同小奖":"下局再试");
    finish_action(message,lines>0);refresh_hint();
}
static void start_slot(void)
{
    uint32_t seed=esp_random();
    if(s_slot_mode) {
        s_multi_result=multi_slot_make_result(esp_random());multi_show_wins(false);
        for(int c=0;c<5;c++) {
            reel_motion_t *m=&s_multi_motion[c];
            uint8_t target[3];for(int r=0;r<3;r++)target[r]=s_multi_result.cells[r*5+c];
            multi_slot_plan_t plan=multi_slot_prepare_strip(s_multi_strip[c],m->phase_q8,(uint8_t)c,target,esp_random());
            m->start_q8=plan.start_q8;m->end_q8=plan.end_q8;s_multi_rows[c][0].stopped=false;
            m->profile.stop_ms=(uint16_t)(speed_duration(2700,1900,1100)+c*160+esp_random()%180);
        }
    } else {
        s_slot_result=slot_make_result(esp_random(),esp_random(),esp_random());
        for(int i=0;i<3;i++) {
            reel_motion_t *m=&s_reel_motion[i];m->profile=motion_slot_profile(seed,s_speed,(uint8_t)i);
            m->profile.stop_ms=(uint16_t)(speed_duration(2600,1800,1100)+i*260+(seed>>(i*4))%280);
            m->start_q8=m->phase_q8;
            m->end_q8=((m->start_q8/256/SLOT_SYMBOL_COUNT+5+i)*SLOT_SYMBOL_COUNT+s_slot_result.reels[i])*256;
            s_reels[i].stopped=false;
        }
    }
    s_busy=true;s_started=lv_tick_get();lv_label_set_text(s_status,s_slot_mode?"五列转动中":"卷带转动中");refresh_hint();sfx(SFX_START);
}
static void animate_slot(uint32_t elapsed)
{
    bool all_stopped=true;
    int columns=s_slot_mode?5:3;
    for(int c=0;c<columns;c++) {
        reel_motion_t *m=s_slot_mode?&s_multi_motion[c]:&s_reel_motion[c];
        m->phase_q8=motion_reel_position(m->start_q8,m->end_q8,elapsed,m->profile.stop_ms);
        if(s_slot_mode)multi_reel_layout(c,m->phase_q8);else reel_layout(c,m->phase_q8);
        if(elapsed<m->profile.stop_ms)all_stopped=false;
        else if(s_slot_mode&&!s_multi_rows[c][0].stopped){s_multi_rows[c][0].stopped=true;sfx(SFX_STOP);}
        else if(!s_slot_mode&&!s_reels[c].stopped){s_reels[c].stopped=true;sfx(SFX_STOP);}
    }
    if(all_stopped)finish_slot_round();
}
static void start_roulette(void)
{
    uint32_t seed=esp_random();s_roulette_result=chance_roulette(esp_random());
    int pocket=0;for(int i=0;i<37;i++)if(t3d_roulette_number(i)==s_roulette_result.number)pocket=i;
    s_roulette_motion.profile.duration_ms=(uint16_t)(speed_duration(5800,4200,2800)+seed%500);
    s_wheel_start=(int32_t)(seed%3600);s_wheel_end=s_wheel_start+3*3600+(int32_t)(esp_random()%3600);
    int target=(s_wheel_end+pocket*3600/37+48)%3600;
    s_ball_start=9*3600+(int32_t)(esp_random()%3600);
    s_ball_end=s_ball_start-6*3600-((s_ball_start-target+3600)%3600);
    s_busy=true;s_started=lv_tick_get();lv_label_set_text(s_status,"小球旋转中");sfx(SFX_START);
}
static void animate_roulette(uint32_t elapsed)
{
    uint32_t duration=s_roulette_motion.profile.duration_ms;
    int wheel=(int)motion_reel_position((uint32_t)s_wheel_start,(uint32_t)s_wheel_end,elapsed,duration);
    uint32_t travelled=motion_reel_position(0,(uint32_t)(s_ball_start-s_ball_end),elapsed,duration);
    int ball=s_ball_start-(int)travelled;
    uint32_t t=elapsed>duration?duration:elapsed;
    int radius=t<duration*7/10?85:85-(int)((uint64_t)(t-duration*7/10)*25/(duration-duration*7/10));
    t3d_render_roulette(&s_dice_surface,wheel,ball,radius);lv_obj_invalidate(s_dice_canvas);
    if(elapsed>=duration) {
        const char *color=s_roulette_result.color==CHANCE_RED?"红色":s_roulette_result.color==CHANCE_BLACK?"黑色":"绿色";
        char text[32];snprintf(text,sizeof(text),"%s %u 号",color,s_roulette_result.number);
        ESP_LOGI(TAG,"roulette number=%u wheel=%d ball=%d radius=%d",s_roulette_result.number,wheel%3600,ball%3600,radius);
        finish_action(text,s_roulette_result.number==0);
    }
}
static void start_dice(void)
{
    dice_place_initial(true);memset(s_die_result,0,sizeof(s_die_result));
    s_dice_deadline=6200;s_dice_last_us=esp_timer_get_time();s_dice_accum_us=0;
    s_busy=true;s_started=lv_tick_get();lv_label_set_text(s_status,"骰子滚动中");sfx(SFX_START);
}
static void animate_dice(uint32_t elapsed)
{
    int64_t now=esp_timer_get_time(),delta=now-s_dice_last_us;s_dice_last_us=now;
    if(delta<0){delta=0;}
    if(delta>120000){delta=120000;}
    s_dice_accum_us+=delta;
    uint16_t strongest=0;
    int steps=0;
    while(s_dice_accum_us>=16667&&steps++<8) {
        for(int i=0;i<s_dice_count;i++) {
            uint16_t hit=rigid_die_step_60hz(&s_rigid_dice[i]);if(hit>strongest)strongest=hit;
            for(int j=i+1;j<s_dice_count;j++) {hit=rigid_die_pair_step(&s_rigid_dice[i],&s_rigid_dice[j]);if(hit>strongest)strongest=hit;}
        }
        s_dice_accum_us-=16667;
    }
    if(strongest>30)sfx_intensity(SFX_BOUNCE,(uint8_t)(strongest>170?255:85+strongest));
    bool settled=true;for(int i=0;i<s_dice_count;i++)if(!s_rigid_dice[i].sleeping)settled=false;
    bool finished=settled||elapsed>=s_dice_deadline;
    if(finished)for(int i=0;i<s_dice_count;i++)rigid_die_settle(&s_rigid_dice[i]);
    t3d_render_dice_count(&s_dice_surface,s_rigid_dice,s_dice_count);lv_obj_invalidate(s_dice_canvas);
    if(!finished)return;
    unsigned total=0;char values[32]={0};size_t used=0;
    for(int i=0;i<s_dice_count;i++) {s_die_result[i]=rigid_die_top_face(&s_rigid_dice[i]);total+=s_die_result[i];used+=(size_t)snprintf(values+used,sizeof(values)-used,"%s%u",i?",":"",s_die_result[i]);}
    ESP_LOGI(TAG,"dice count=%u faces=%s total=%u natural=%d",s_dice_count,values,total,settled);
    char text[32];snprintf(text,sizeof(text),"%u 枚共 %u 点",s_dice_count,total);finish_action(text,false);
}
static void plinko_place_ball(void)
{
    int size=s_plinko_count==1?10:7;
    unsigned parked[9]={0};
    for(int i=0;i<s_plinko_count;i++) {
        plinko_ball_t *b=&s_plinko_physics[i];
        int x=b->x/256-size/2,y=b->y/256-size/2;
        if(b->done&&s_plinko_count>1) {
            unsigned place=parked[b->bin]++;
            x=18+b->bin*20+(place%2)*7;
            y=143-(place/2)*7;
        }
        lv_obj_set_pos(s_plinko_ball[i],x,y);
        lv_obj_set_pos(s_plinko_highlight[i],x+1,y+1);
    }
}
static void start_plinko(void)
{
    plinko_balls_init(s_plinko_physics,s_plinko_count,esp_random(),s_speed);
    s_plinko_last_us=esp_timer_get_time();s_plinko_accum_us=0;
    for(int i=0;i<9;i++)lv_obj_set_style_bg_color(s_plinko_bins[i],lv_color_hex(C_PANEL),0);
    plinko_place_ball();s_busy=true;s_started=lv_tick_get();
    lv_label_set_text(s_status,"弹珠下落中");sfx(SFX_START);
}
static void animate_plinko(uint32_t elapsed)
{
    (void)elapsed;
    int64_t now=esp_timer_get_time(),delta=now-s_plinko_last_us;s_plinko_last_us=now;
    if(delta<0)delta=0;
    if(delta>120000)delta=120000;
    s_plinko_accum_us+=delta;
    int steps=0;uint16_t hits=0;
    while(s_plinko_accum_us>=16667&&steps++<8) {
        for(int i=0;i<s_plinko_count;i++) {
            plinko_ball_t *b=&s_plinko_physics[i];bool was_done=b->done;
            hits+=plinko_ball_step(b);
            if(!was_done&&b->done) {
                lv_obj_set_style_bg_color(s_plinko_bins[b->bin],lv_color_hex(C_MAGENTA),0);
                ESP_LOGI(TAG,"plinko ball=%u count=%u bin=%u collisions=%u ticks=%u duration_ms=%lu",i+1,s_plinko_count,b->bin+1,b->collisions,b->ticks,(unsigned long)(b->ticks*1000/60));
            }
        }
        s_plinko_accum_us-=16667;
    }
    if(hits)sfx_intensity(SFX_PLINK,160);
    plinko_place_ball();
    unsigned done=0,center=0;
    for(int i=0;i<s_plinko_count;i++)if(s_plinko_physics[i].done){done++;if(s_plinko_physics[i].bin==4)center++;}
    if(done==s_plinko_count) {
        char text[32];
        if(s_plinko_count==1)snprintf(text,sizeof(text),"落入第 %u 格",s_plinko_physics[0].bin+1);
        else snprintf(text,sizeof(text),"%u 球全部落盘",s_plinko_count);
        ESP_LOGI(TAG,"plinko_round count=%u landed=%u center=%u",s_plinko_count,done,center);
        finish_action(text,center>0);
    } else lv_label_set_text_fmt(s_status,"已落盘 %u/%u 球",done,s_plinko_count);
}

static void start_current_game(void)
{
    if (s_busy || s_page == PAGE_HOME) return;
    ESP_LOGI(TAG, "play page=%d speed=%u", s_page, s_speed);
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

    animate_neon(now);

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
    if(s_page==PAGE_SETTINGS) {
        if(input->event==BSP_BTN_LONG&&input->button==BSP_BTN_OK){load_page(PAGE_HOME);sfx(SFX_UI);return;}
        if(input->event==BSP_BTN_CLICK) {
            if(input->button==BSP_BTN_UP)s_settings_index=(s_settings_index+SETTINGS_ROWS-1)%SETTINGS_ROWS;
            else if(input->button==BSP_BTN_DOWN)s_settings_index=(s_settings_index+1)%SETTINGS_ROWS;
            else if(input->button==BSP_BTN_OK) {
                switch(s_settings_index){
                case 0:s_fps_display_enabled=!s_fps_display_enabled;break;
                case 1:s_dice_count=s_dice_count%6+1;break;
                case 2:s_neon_mode=(s_neon_mode+1)%3;break;
                case 3:s_sound=!s_sound;break;
                case 4:s_speed=(s_speed+1)%3;break;
                case 5:s_slot_mode^=1;break;
                case 6:s_plinko_count=s_plinko_count%PLINKO_MAX_BALLS+1;break;
                }
            }
            build_settings();sfx(SFX_UI);
        }
        return;
    }

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
        } else if (input->event == BSP_BTN_LONG && input->button == BSP_BTN_OK) {
            load_page(PAGE_SETTINGS);
            sfx(SFX_UI);
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
        if(s_page==PAGE_DICE){s_dice_count=s_dice_count%6+1;build_dice();}
        else if(s_page==PAGE_PLINKO){s_plinko_count=s_plinko_count%PLINKO_MAX_BALLS+1;load_page(PAGE_PLINKO);}
        else s_speed = (uint8_t)((s_speed + 1u) % 3u);
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

        int64_t now = esp_timer_get_time() / 1000;
        if (now >= battery_due) {
            battery_due = now + 30000;
            if (bsp_lvgl_lock(250)) {
                update_battery_unlocked();
                bsp_lvgl_unlock();
            }
        }
    }
}

static bool check_fonts(void)
{
    const uint32_t *inventory=ARCADE_CHINESE_CODEPOINTS;
    unsigned count=sizeof(ARCADE_CHINESE_CODEPOINTS)/sizeof(ARCADE_CHINESE_CODEPOINTS[0]);
    const lv_font_t *fonts[]={&arcade_font_14,&arcade_font_20};
    unsigned missing=0;
    for(unsigned f=0;f<2;f++)for(unsigned i=0;i<count;i++){
        lv_font_glyph_dsc_t glyph={0};
        if(!lv_font_get_glyph_dsc(fonts[f],&glyph,inventory[i],0)||glyph.is_placeholder){++missing;ESP_LOGE(TAG,"Missing font=%u U+%04lx",f,(unsigned long)inventory[i]);}
    }
    lv_font_glyph_dsc_t absent={0};
    bool negative=!lv_font_get_glyph_dsc(&arcade_font_14,&absent,0x9F98,0)||absent.is_placeholder;
    ESP_LOGI(TAG,"FONT_AUDIT glyphs=%u fonts=2 missing=%u negative_ok=%d",count,missing,negative);
    return missing==0&&negative;
}
#if CONFIG_ARCADE_USB_TEST
static lv_area_t s_audit_rects[48];
static unsigned s_audit_count,s_audit_clipped,s_audit_overlap;
static void audit_labels(lv_obj_t *parent)
{
    for(unsigned i=0;i<lv_obj_get_child_count(parent);i++) {
        lv_obj_t *child=lv_obj_get_child(parent,i);
        if(lv_obj_check_type(child,&lv_label_class)&&lv_label_get_text(child)[0]) {
            lv_area_t rect,clip;lv_obj_get_coords(child,&rect);clip=rect;
            if(lv_label_get_long_mode(child)==LV_LABEL_LONG_MODE_CLIP) {
                lv_point_t text_size;
                lv_text_get_size(&text_size,lv_label_get_text(child),lv_obj_get_style_text_font(child,0),
                    lv_obj_get_style_text_letter_space(child,0),lv_obj_get_style_text_line_space(child,0),LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                if(text_size.x>lv_obj_get_content_width(child)||text_size.y>lv_obj_get_content_height(child))s_audit_clipped++;
            }
            for(lv_obj_t *p=lv_obj_get_parent(child);p;p=lv_obj_get_parent(p)){
                lv_area_t bounds;lv_obj_get_coords(p,&bounds);
                if(clip.x1<bounds.x1){clip.x1=bounds.x1;}
                if(clip.y1<bounds.y1){clip.y1=bounds.y1;}
                if(clip.x2>bounds.x2){clip.x2=bounds.x2;}
                if(clip.y2>bounds.y2){clip.y2=bounds.y2;}
            }
            if(memcmp(&clip,&rect,sizeof(rect))||rect.x1<0||rect.y1<0||rect.x2>=240||rect.y2>=320)s_audit_clipped++;
            for(unsigned j=0;j<s_audit_count;j++){
                lv_area_t *a=&s_audit_rects[j];
                if(rect.x1<=a->x2&&rect.x2>=a->x1&&rect.y1<=a->y2&&rect.y2>=a->y1)s_audit_overlap++;
            }
            if(s_audit_count<48)s_audit_rects[s_audit_count++]=rect;
        }
        audit_labels(child);
    }
}
static void audit_ui(void)
{
    lv_obj_update_layout(s_screen);s_audit_count=s_audit_clipped=s_audit_overlap=0;audit_labels(s_screen);
    ESP_LOGI(TAG,"UI_AUDIT page=%d labels=%u clipped=%u overlaps=%u cube_mesh=%d",s_page,s_audit_count,s_audit_clipped,s_audit_overlap,t3d_cube_mesh_valid());
}
#endif

#if CONFIG_ARCADE_USB_TEST
/* Development-only USB input follows the same queue and LVGL ownership as keys. */
static void usb_test_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t command;
        if (usb_serial_jtag_read_bytes(&command, 1, pdMS_TO_TICKS(100)) != 1) continue;
        if(command=='v'){if(bsp_lvgl_lock(500)){audit_ui();bsp_lvgl_unlock();}continue;}
        if (command == '?') {
            if (bsp_lvgl_lock(500)) {
                lv_mem_monitor_t memory;
                lv_mem_monitor(&memory);
                ESP_LOGI(TAG, "state page=%d selected=%u busy=%d auto=%d speed=%u sound=%d fps_overlay=%d completed=%lu heap=%lu min=%lu largest=%lu lvfree=%lu stack=%u audio=%d writes=%u audio_errors=%u lvpeak=%lu lvlargest=%lu dice_count=%u neon=%u backlight=100 slot_mode=%u round_score=%lu total_classic=%lu total_multi=%lu settings_row=%u plinko_count=%u",
                         s_page, s_home_index, s_busy, s_auto, s_speed, (bool)s_sound,
                         s_fps_display_enabled,
                         (unsigned long)s_completed,
                         (unsigned long)esp_get_free_heap_size(),
                         (unsigned long)esp_get_minimum_free_heap_size(),
                         (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                         (unsigned long)memory.free_size,
                         (unsigned)uxTaskGetStackHighWaterMark(NULL), (bool)s_audio_ready,
                         (unsigned)s_audio_writes, (unsigned)s_audio_errors,
                         (unsigned long)memory.max_used, (unsigned long)memory.free_biggest_size, s_dice_count, s_neon_mode, s_slot_mode, (unsigned long)s_slot_last_score[s_slot_mode], (unsigned long)s_slot_total[0], (unsigned long)s_slot_total[1], s_settings_index, s_plinko_count);
                bsp_lvgl_unlock();
            }
            continue;
        }
        if (command == 'm') {
            if (!bsp_lvgl_lock(500)) continue;
            if (!is_game_page(s_page)) {
                ESP_LOGW(TAG, "FPS measurement requires a game page");
                bsp_lvgl_unlock();
                continue;
            }
            s_fps_page = s_page;
            s_fps_frames = 0;
            s_fps_active_ms = 0;
            s_fps_last_frame_ms = 0;
            s_fps_start_us = esp_timer_get_time();
            s_fps_sampling = true;
            ESP_LOGI(TAG, "FPS measurement started: %s refreshes for 10 seconds",
                     fps_page_name(s_fps_page));
            bsp_lvgl_unlock();

            vTaskDelay(pdMS_TO_TICKS(10000));

            if (!bsp_lvgl_lock(500)) continue;
            s_fps_sampling = false;
            uint32_t elapsed_ms = (uint32_t)((esp_timer_get_time() - s_fps_start_us) / 1000);
            uint32_t fps_x100 = s_fps_active_ms && s_fps_frames > 1
                ? (uint32_t)(((uint64_t)(s_fps_frames - 1) * 100000u) / s_fps_active_ms)
                : 0;
            ESP_LOGI(TAG, "FPS_RESULT page=%s slot_mode=%u window_ms=%lu active_ms=%lu frames=%lu fps_x100=%lu",
                     fps_page_name(s_fps_page), s_slot_mode,
                     (unsigned long)elapsed_ms, (unsigned long)s_fps_active_ms,
                     (unsigned long)s_fps_frames, (unsigned long)fps_x100);
            bsp_lvgl_unlock();
            continue;
        }
        input_event_t input = { .event = BSP_BTN_CLICK };
        switch (command) {
        case 'u': case 'U': input.button = BSP_BTN_UP; break;
        case 'd': case 'D': input.button = BSP_BTN_DOWN; break;
        case 'o': case 'O': input.button = BSP_BTN_OK; break;
        default: continue;
        }
        if (command >= 'A' && command <= 'Z') input.event = BSP_BTN_LONG;
        if (s_input_ready && xQueueSend(s_input_queue, &input, pdMS_TO_TICKS(100)) != pdTRUE)
            ESP_LOGW(TAG, "USB test input queue full");
    }
}
#endif

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

    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "display initialization failed");
        return;
    }
    lv_display_t *display = bsp_lvgl_init();
    if (!display) {
        ESP_LOGE(TAG, "display/LVGL initialization failed");
        return;
    }
    bsp_display_backlight(100);
    if(!check_fonts())return;
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
    lv_display_add_event_cb(display, fps_refresh_event_cb, LV_EVENT_REFR_READY, NULL);
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
#if CONFIG_ARCADE_USB_TEST
    usb_serial_jtag_driver_config_t usb_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&usb_config) != ESP_OK) {
        ESP_LOGE(TAG, "USB test driver initialization failed");
    } else if (xTaskCreate(usb_test_task, "arcade_usb", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "USB test task allocation failed");
        usb_serial_jtag_driver_uninstall();
    } else {
        ESP_LOGI(TAG, "USB test keys: u/d/o click, U/D/O hold, ? state");
    }
#endif
}
