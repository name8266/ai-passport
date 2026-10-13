// 数字生命实验室：全新研究终端 UI，不复用基线 demo 页面。
// 固件逻辑和输入由单一应用任务串行持有；LVGL 操作必须获得 BSP 锁。
#include "binary_life.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "lvgl.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>

#define INPUT_QUEUE_LENGTH 12
#define TICK_MS 250
#define CHECKPOINT_GENERATIONS 120u
#define COLOR_BACKGROUND 0x081321u
#define COLOR_PANEL 0x102339u
#define COLOR_MINT 0x9DEBCEu
#define COLOR_TEXT 0xDBE7F1u
#define COLOR_MUTED 0x92A9C0u

static const char *TAG = "binary_life";
typedef struct {
    bsp_btn_t button;
    bsp_btn_ev_t event;
} input_t;

// 全部可变状态只允许 s_runner 任务读写；绘图时持 LVGL 锁。
static bl_world_t s_world;
static bl_snapshot_t s_checkpoint;
static QueueHandle_t s_queue;
static lv_obj_t *s_screen;
static lv_obj_t *s_battery;
static lv_obj_t *s_tabs;
static lv_obj_t *s_headline;
static lv_obj_t *s_board;
static lv_obj_t *s_detail;
static lv_obj_t *s_footer;
static uint8_t s_page;
static uint16_t s_sample;
static uint32_t s_saved_generation;
static bool s_storage_ok;
static nvs_handle_t s_store;
static lv_font_t s_chinese_font;

static lv_obj_t *new_label(lv_obj_t *parent, int x, int y, int width,
                           uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, &s_chinese_font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

// 可重复的 NVS 快照：单 blob，版本和哈希在纯逻辑层校验。
// 常规每 120 代自动保存；暂停与设置变化立即保存。Flash 有磨损上限。
static bool save_world(void)
{
    if (!s_storage_ok) return false;
    bl_snapshot_encode(&s_world, &s_checkpoint);
    esp_err_t err = nvs_set_blob(s_store, "world", &s_checkpoint,
                                 sizeof(s_checkpoint));
    if (err == ESP_OK) err = nvs_commit(s_store);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存失败: %s", esp_err_to_name(err));
        return false;
    }
    s_saved_generation = s_world.generation;
    return true;
}

static void load_or_seed(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) {
        err = nvs_open("life_lab", NVS_READWRITE, &s_store);
        s_storage_ok = err == ESP_OK;
    }
    if (s_storage_ok) {
        size_t length = sizeof(s_checkpoint);
        err = nvs_get_blob(s_store, "world", &s_checkpoint, &length);
        if (err == ESP_OK && length == sizeof(s_checkpoint) &&
            bl_snapshot_decode(&s_world, &s_checkpoint)) {
            s_saved_generation = s_world.generation;
            ESP_LOGI(TAG, "恢复实验 seed=%08" PRIX32 " gen=%" PRIu32,
                     s_world.seed, s_world.generation);
            return;
        }
        if (err != ESP_ERR_NVS_NOT_FOUND)
            ESP_LOGW(TAG, "旧记录无效，创建新的实验");
    } else {
        ESP_LOGW(TAG, "NVS不可用，实验仅在 RAM 运行: %s", esp_err_to_name(err));
    }
    bl_reset(&s_world, esp_random());
    (void)save_world();
}

// 绘制时只创建一个 LVGL 面板，格子通过 draw descriptor 批处理，
// 无 256 个 LVGL 对象的额外动态内存（适配无 PSRAM 的芯片）。
static void board_draw(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) return;
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t position;
    lv_obj_get_coords(s_board, &position);

    lv_draw_rect_dsc_t rectangle;
    lv_draw_rect_dsc_init(&rectangle);
    rectangle.radius = 2;
    rectangle.bg_opa = LV_OPA_COVER;
    rectangle.border_width = 0;

    for (unsigned index = 0; index < BL_CELLS; ++index) {
        const bl_cell_t *cell = &s_world.cells[index];
        uint32_t color;
        if (!cell->alive) {
            color = cell->resource > 90 ? 0x1D3852u :
                    cell->resource > 50 ? 0x152B42u : 0x0D1C2Fu;
        } else if (cell->energy < 16) {
            color = 0xD66A68u;
        } else {
            static const uint32_t spectrum[4] = {
                0x6DDEBAu, 0x58C8EBu, 0xBA9AF3u, 0xF2BD73u
            };
            color = spectrum[(cell->genome >> 4) & 3u];
        }
        rectangle.bg_color = lv_color_hex(color);
        int16_t x = (int16_t)(position.x1 + 4 + (index % BL_SIDE) * 12u);
        int16_t y = (int16_t)(position.y1 + 4 + (index / BL_SIDE) * 12u);
        lv_area_t area = {x, y, (int16_t)(x + 9), (int16_t)(y + 9)};
        lv_draw_rect(layer, &rectangle, &area);
    }
}

static void build_screen(void)
{
    // 内置 CJK 子集在 sdkconfig.defaults 显式启用；不依赖系统中不存在的中文字体。
    s_chinese_font = lv_font_source_han_sans_sc_16_cjk;
    s_chinese_font.fallback = &lv_font_montserrat_14;

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = new_label(s_screen, 13, 8, 172, COLOR_MINT);
    lv_label_set_text(name, "二进制生命实验");

    s_battery = new_label(s_screen, 186, 8, 49, COLOR_MUTED);
    lv_obj_set_style_text_align(s_battery, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *line = lv_obj_create(s_screen);
    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 13, 37);
    lv_obj_set_size(line, 214, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(0x35556Bu), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

    s_tabs = new_label(s_screen, 14, 43, 224, COLOR_MINT);
    s_headline = new_label(s_screen, 14, 65, 223, COLOR_TEXT);

    s_board = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_board);
    lv_obj_set_pos(s_board, 20, 84);
    lv_obj_set_size(s_board, 200, 200);
    lv_obj_set_style_bg_color(s_board, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(s_board, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_board, board_draw, LV_EVENT_DRAW_MAIN, NULL);

    s_detail = new_label(s_screen, 18, 91, 207, COLOR_TEXT);
    s_footer = new_label(s_screen, 13, 289, 222, COLOR_MUTED);

    lv_screen_load(s_screen);
}

// 中文页内容包括可读的基因、环境指标与实验控制，非电子宠物 UI。
static void refresh_screen(void)
{
    bl_stats_t stats;
    bl_stats(&s_world, &stats);
    int percent = bsp_battery_soc();
    if (percent >= 0 && percent <= 100)
        lv_label_set_text_fmt(s_battery, "%d%%", percent);
    else
        lv_label_set_text(s_battery, "--");

    static const char *tab_names[3] = {
        "[生态]  基因   设置",
        " 生态  [基因]  设置",
        " 生态   基因  [设置]"
    };
    lv_label_set_text(s_tabs, tab_names[s_page]);

    if (s_page == 0) {
        lv_obj_remove_flag(s_board, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_headline, "代 %" PRIu32 "   数 %u   型 %u",
                              s_world.generation, (unsigned)stats.population,
                              (unsigned)stats.genotypes);
        lv_label_set_text(s_footer, s_world.paused
            ? "上/下 切页  确定:继续"
            : "上/下 切页  确定:暂停");
        lv_obj_invalidate(s_board);
    } else if (s_page == 1) {
        lv_obj_add_flag(s_board, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_headline, "样本分析  %u/%u",
            (unsigned)(stats.population ? 1u : 0u),
            (unsigned)stats.population);
        if (stats.population == 0) {
            lv_label_set_text(s_detail, "实验群体已全部死亡\n\n可在设置页重新开始实验");
        } else {
            // 样本游标是格子索引，失活时寻找下一名存活个体。
            unsigned cursor = s_sample;
            for (unsigned n = 0; n < BL_CELLS; ++n) {
                unsigned candidate = (cursor + n) % BL_CELLS;
                if (s_world.cells[candidate].alive) {
                    s_sample = (uint16_t)candidate;
                    break;
                }
            }
            const bl_cell_t *c = &s_world.cells[s_sample];
            char bits[17];
            bl_genome_bits(c->genome, bits);
            lv_label_set_text_fmt(s_detail,
                "基因 %s\n\n吸收 %u   转化 %u\n复制值 %u   寿命 %u\n"
                "个体能量 %u  年龄 %u\n\n"
                "出生 %" PRIu32 "   死亡 %" PRIu32 "\n突变 %" PRIu32,
                bits,
                1u + (c->genome & 3u), 1u + ((c->genome >> 4) & 3u),
                48u + 3u * ((c->genome >> 8) & 15u),
                30u + 6u * ((c->genome >> 12) & 15u),
                (unsigned)c->energy, (unsigned)c->age,
                s_world.births, s_world.deaths, s_world.mutations);
        }
        lv_label_set_text(s_footer, "上/下 切页   确定:换样本");
    } else {
        lv_obj_add_flag(s_board, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_detail, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_headline, "实验参数  /  操作记录");
        static const char *rates[4] = {"关闭", "1/512", "1/128", "1/32"};
        lv_label_set_text_fmt(s_detail,
            "随机种子 %08" PRIX32 "\n"
            "当前代数 %" PRIu32 "\n\n"
            "突变率 %s\n"
            "资源环境 %s\n"
            "平均能量 %u\n\n"
            "确定:修改突变率\n"
            "长按上键:改变环境\n"
            "长按确定:开始新实验",
            s_world.seed, s_world.generation,
            rates[s_world.mutation_level],
            s_world.climate ? "周期变化" : "稳定",
            (unsigned)stats.mean_energy);
        lv_label_set_text(s_footer,
            s_storage_ok ? "自动保存:每120代" : "存储失败:临时实验");
    }
}

static void refresh_locked(void)
{
    if (bsp_lvgl_lock(150)) {
        refresh_screen();
        bsp_lvgl_unlock();
    }
}

// ESP timer 共享回调中禁止刷新 GUI 或写 NVS；仅推入事件队列。
static void on_button(bsp_btn_t button, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!s_queue || (event != BSP_BTN_CLICK && event != BSP_BTN_LONG)) return;
    input_t input = {.button = button, .event = event};
    (void)xQueueSend(s_queue, &input, 0);
}

static void handle_button(input_t input)
{
    bool save_now = false;
    if (input.event == BSP_BTN_CLICK) {
        if (input.button == BSP_BTN_UP)
            s_page = (uint8_t)((s_page + 2u) % 3u);
        else if (input.button == BSP_BTN_DOWN)
            s_page = (uint8_t)((s_page + 1u) % 3u);
        else if (input.button == BSP_BTN_OK) {
            if (s_page == 0) {
                s_world.paused = !s_world.paused;
                save_now = true;
            } else if (s_page == 1) {
                s_sample = (uint16_t)((s_sample + 1u) % BL_CELLS);
            } else {
                s_world.mutation_level = (s_world.mutation_level + 1u) % 4u;
                save_now = true;
            }
        }
    } else if (input.event == BSP_BTN_LONG && s_page == 2) {
        if (input.button == BSP_BTN_UP) {
            s_world.climate ^= 1u;
            save_now = true;
        } else if (input.button == BSP_BTN_OK) {
            bl_reset(&s_world, esp_random());
            s_sample = 0;
            save_now = true;
        }
    }
    if (save_now) (void)save_world();
    bsp_display_backlight(70);
    refresh_locked();
}

// 唯一业务任务：周期演化、按键处理、间隔持久化。无网络服务/后台云请求。
static void runner(void *unused)
{
    (void)unused;
    input_t input;
    TickType_t last_input = xTaskGetTickCount();
    for (;;) {
        if (xQueueReceive(s_queue, &input, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            handle_button(input);
            last_input = xTaskGetTickCount();
        } else {
            // 世界模拟也处于同一 LVGL 锁保护下，绘制回调不会看到半更新的数据。
            if (bsp_lvgl_lock(150)) {
                bl_step(&s_world);
                refresh_screen();
                bsp_lvgl_unlock();
            }
            if (s_world.generation - s_saved_generation >= CHECKPOINT_GENERATIONS)
                (void)save_world();
            // 不影响实验持续运行，仅降低空闲屏幕的背光功耗。
            if (xTaskGetTickCount() - last_input > pdMS_TO_TICKS(120000))
                bsp_display_backlight(10);
            else if (xTaskGetTickCount() - last_input > pdMS_TO_TICKS(45000))
                bsp_display_backlight(25);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "二进制生命实验室启动 (ESP32-C3)");
    (void)bsp_i2c_init();
    (void)bsp_battery_init();
    load_or_seed();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "LCD/LVGL初始化失败, MOSI=%d SCLK=%d CS=%d",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS);
        if (s_storage_ok) nvs_close(s_store);
        return;
    }
    bsp_display_backlight(70);

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL 锁失败");
        return;
    }
    build_screen();
    refresh_screen();
    bsp_lvgl_unlock();

    s_queue = xQueueCreate(INPUT_QUEUE_LENGTH, sizeof(input_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "输入队列创建失败");
        return;
    }
    if (xTaskCreate(runner, "life_lab", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "实验任务创建失败");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return;
    }
    if (bsp_button_init(on_button, NULL) != ESP_OK)
        ESP_LOGE(TAG, "按键初始化失败;模拟仍会继续运行");
}
