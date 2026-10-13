// AI Passport: live Avida-inspired executable organisms and competition dashboard.
// This application deliberately uses no baseline demo UI, Wi-Fi or cloud API.
// Core VM is host-testable; UI and persistence use BSP + esp_partition.
#include "avida_vm.h"
#include "bsp_i2c.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TICK_MILLISECONDS 200u
#define INSTRUCTIONS_PER_TICK 1536u
#define AUTOSAVE_MILLISECONDS (10u * 60u * 1000u)
#define DATA_SLOT_SIZE 0x20000u
#define DATA_PARTITION_SIZE 0x40000u
#define FLASH_SECTOR 4096u
#define COLOR_BG 0x071520u
#define COLOR_MUTED 0x86ACC0u
#define COLOR_WHITE 0xE7F2F4u
#define COLOR_HIGHLIGHT 0x71F0D1u

static const char *TAG = "avida_v2";
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; } key_msg_t;

static av_world_t s_world;
static av_snapshot_t s_snapshot;
static const esp_partition_t *s_partition;
static uint32_t s_saved_sequence;
static uint32_t s_last_save_ms;
static uint32_t s_last_export_ms;
static QueueHandle_t s_input;
static lv_obj_t *s_root, *s_battery, *s_tabs, *s_header;
static lv_obj_t *s_grid, *s_detail, *s_footer;
static lv_font_t s_font;
static uint8_t s_page, s_visual_mode;
static uint16_t s_focus;
static int s_cached_soc=-1;
static int64_t s_soc_time;

// Two alternating independent erase/write slots: interrupted saves do not
// erase the previous valid snapshot. Records include full CPU state + RNG.
static bool persistence_read(void)
{
    if (!s_partition || sizeof(s_snapshot)>DATA_SLOT_SIZE) return false;
    bool found=false;
    uint32_t newest=0;
    for (unsigned slot=0;slot<2;++slot) {
        esp_err_t err=esp_partition_read(s_partition,
                              (size_t)slot * DATA_SLOT_SIZE,
                              &s_snapshot,sizeof(s_snapshot));
        if(err!=ESP_OK) continue;
        if(s_snapshot.magic!=AV_SAVE_MAGIC ||
           s_snapshot.version!=AV_SAVE_VERSION) continue;
        if(!found || (int32_t)(s_snapshot.sequence-newest)>0) {
            if(av_snapshot_decode(&s_world,&s_snapshot)) {
                newest=s_snapshot.sequence;
                found=true;
            }
        }
    }
    if(found){s_saved_sequence=newest;}
    return found;
}

static bool persistence_write(void)
{
    if(!s_partition || sizeof(s_snapshot)>DATA_SLOT_SIZE)return false;
    uint32_t seq=s_saved_sequence+1u;
    unsigned slot=seq & 1u;
    size_t offset=(size_t)slot * DATA_SLOT_SIZE;
    size_t erase=((sizeof(s_snapshot)+FLASH_SECTOR-1u)/FLASH_SECTOR)*FLASH_SECTOR;
    av_snapshot_encode(&s_world,seq,&s_snapshot);
    esp_err_t err=esp_partition_erase_range(s_partition,offset,erase);
    if(err==ESP_OK)err=esp_partition_write(s_partition,offset,
                                           &s_snapshot,sizeof(s_snapshot));
    if(err!=ESP_OK){
        ESP_LOGE(TAG,"记录存储失败: %s",esp_err_to_name(err));
        return false;
    }
    s_saved_sequence=seq;
    s_has_snapshot=true;
    s_last_save_ms=(uint32_t)(esp_timer_get_time()/1000);
    return true;
}

static void init_experiment(void)
{
    s_partition=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                (esp_partition_subtype_t)0x40,"life_data");
    if(!s_partition||s_partition->size<DATA_PARTITION_SIZE)
        ESP_LOGE(TAG,"life_data 分区缺失: 仅运行临时实验");
    if(persistence_read()){
        ESP_LOGI(TAG,"恢复 V2 快照 #%"PRIu32" seed=%08"PRIX32
                 " instr=%"PRIu32,s_saved_sequence,s_world.seed,s_world.instructions);
        return;
    }
    av_reset(&s_world,esp_random());
    s_saved_sequence=0;
    (void)persistence_write();
}

static lv_obj_t *label(lv_obj_t *parent,int x,int y,int width,
                       uint32_t color)
{
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_pos(o,x,y);
    lv_obj_set_width(o,width);
    lv_obj_set_style_text_font(o,&s_font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);
    return o;
}

// One custom drawing object for all 256 cells. Hue = lineage identity;
// brightness and highlights = program execution/replication events.
static void draw_world(lv_event_t *event)
{
    if(lv_event_get_code(event)!=LV_EVENT_DRAW_MAIN)return;
    static const uint32_t hues[]={
        0x56DFBFu,0x63BCEEu,0xD6A2EFu,0xEAB75Fu,
        0xE38C8Cu,0xA7D767u,0x78C9B0u,0xD5C97Au
    };
    lv_layer_t *layer=lv_event_get_layer(event);
    lv_area_t bounds;
    lv_obj_get_coords(s_grid,&bounds);
    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa=LV_OPA_COVER;
    rect.border_width=0;
    rect.radius=2;
    uint16_t last_site=AV_SITES;
    if(s_world.event_count) {
        unsigned last=(s_world.event_head+AV_EVENT_CAP-1u)%AV_EVENT_CAP;
        last_site=s_world.events[last].site;
    }
    for(unsigned i=0;i<AV_SITES;++i) {
        const av_organism_t *o=&s_world.organisms[i];
        if(!o->length)rect.bg_color=lv_color_hex(0x132A3Au);
        else if(i==last_site)rect.bg_color=lv_color_hex(0xFFFFFFu);
        else {
            unsigned idx=(unsigned)((o->lineage*2654435761u)>>29u)%8u;
            uint32_t color=hues[idx];
            if(s_visual_mode==1) {
                // Copy progress: green while offspring memory is filling.
                color=o->allocated && o->copied?
                    0x8EF48Du:0x34536Bu;
            }else if(s_visual_mode==2) {
                // Alternative visualization: length bins classify code complexity.
                color=hues[(o->length/8u)%8u];
            }
            rect.bg_color=lv_color_hex(color);
        }
        int16_t x=(int16_t)(bounds.x1+4+(i%AV_SIDE)*12u);
        int16_t y=(int16_t)(bounds.y1+4+(i/AV_SIDE)*12u);
        lv_area_t area={x,y,(int16_t)(x+9),(int16_t)(y+9)};
        lv_draw_rect(layer,&rect,&area);
    }
}

static void create_ui(void)
{
    s_font=lv_font_source_han_sans_sc_16_cjk;
    s_font.fallback=&lv_font_montserrat_14;
    s_root=lv_obj_create(NULL);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_style_bg_color(s_root,lv_color_hex(COLOR_BG),0);
    lv_obj_set_style_bg_opa(s_root,LV_OPA_COVER,0);
    lv_obj_clear_flag(s_root,LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title=label(s_root,13,7,188,COLOR_HIGHLIGHT);
    lv_label_set_text(title,"数字生命进化实验");
    s_battery=label(s_root,190,7,44,COLOR_MUTED);
    lv_obj_set_style_text_align(s_battery,LV_TEXT_ALIGN_RIGHT,0);
    s_tabs=label(s_root,13,37,224,COLOR_WHITE);
    s_header=label(s_root,13,59,224,COLOR_MUTED);
    s_grid=lv_obj_create(s_root);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_pos(s_grid,20,82);
    lv_obj_set_size(s_grid,200,200);
    lv_obj_set_style_bg_opa(s_grid,LV_OPA_COVER,0);
    lv_obj_set_style_bg_color(s_grid,lv_color_hex(0x0B1D2Cu),0);
    lv_obj_add_event_cb(s_grid,draw_world,LV_EVENT_DRAW_MAIN,NULL);

    s_detail=label(s_root,14,86,212,COLOR_WHITE);
    lv_obj_add_flag(s_detail,LV_OBJ_FLAG_HIDDEN);
    s_footer=label(s_root,12,289,226,COLOR_MUTED);
    lv_screen_load(s_root);
}

static uint16_t choose_live(uint16_t from)
{
    for(unsigned n=0;n<AV_SITES;++n){
        uint16_t k=(uint16_t)((from+n)%AV_SITES);
        if(s_world.organisms[k].length)return k;
    }
    return 0;
}

static void refresh_ui(void)
{
    const char *tabs[]={"[生态] 基因 数据 控制","生态 [基因] 数据 控制",
                        "生态 基因 [数据] 控制","生态 基因 数据 [控制]"};
    int64_t time_us=esp_timer_get_time();
    if(time_us-s_soc_time>10000000LL) {
        s_cached_soc=bsp_battery_soc();
        s_soc_time=time_us;
    }
    if(s_cached_soc>=0&&s_cached_soc<=100)
        lv_label_set_text_fmt(s_battery,"%d%%",s_cached_soc);
    else lv_label_set_text(s_battery,"--");
    lv_label_set_text(s_tabs,tabs[s_page]);
    if(s_page==0){
        av_stats_t stats;
        av_stats(&s_world,&stats);
        lv_label_set_text_fmt(s_header,"更新 %"PRIu32"  存活 %u  种型 %u",
              s_world.updates,(unsigned)stats.population,
              (unsigned)stats.genotype_count);
        lv_obj_remove_flag(s_grid,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_detail,LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_grid);
        lv_label_set_text(s_footer,s_world.paused?
            "上/下 切页  确定:运行" : "上/下 切页  确定:暂停");
    }else if(s_page==1){
        s_focus=choose_live(s_focus);
        const av_organism_t *o=&s_world.organisms[s_focus];
        lv_obj_add_flag(s_grid,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_detail,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_header,"位置 %03u  个体 #%"PRIu32,
                             (unsigned)s_focus,o->id);
        if(!o->length) {
            lv_label_set_text(s_detail,"无存活个体");
        }else{
            char lines[350];
            int used=snprintf(lines,sizeof(lines),
                 "谱系 #%"PRIu32" 亲代 #%"PRIu32"\n"
                 "指令数 %u 复制 %u/%u\n",
                 o->lineage,o->parent_id,(unsigned)o->length,
                 (unsigned)o->copied,(unsigned)o->length);
            if(used<0)return;
            // Inspect a rolling window around the virtual CPU instruction head.
            for(unsigned i=0;i<6u;++i){
                unsigned ip=(o->ip+i)%o->length;
                char mnemonic[24];
                av_instruction_name(o->code[ip],mnemonic,sizeof(mnemonic));
                size_t end=strlen(lines);
                if(end+30>=sizeof(lines))break;
                (void)snprintf(lines+end,sizeof(lines)-end,
                    "%c %02u  %-12s\n",i==0?'>':' ',
                    ip,mnemonic);
            }
            size_t end=strlen(lines);
            (void)snprintf(lines+end,sizeof(lines)-end,
                "已执行 %"PRIu32" 条",o->instructions);
            lv_label_set_text(s_detail,lines);
        }
        lv_label_set_text(s_footer,"上/下 切页  确定:换样本");
    }else if(s_page==2){
        av_stats_t stats;
        av_stats(&s_world,&stats);
        lv_obj_add_flag(s_grid,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_detail,LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_header,"实时演化与竞争统计");
        const char *event="等待首个复制事件";
        if(s_world.event_count){
            unsigned last=(s_world.event_head+AV_EVENT_CAP-1u)%AV_EVENT_CAP;
            event=av_event_name(s_world.events[last].kind);
        }
        lv_label_set_text_fmt(s_detail,
           "总执行 %"PRIu32"\n"
           "复制出生 %"PRIu32"\n"
           "竞争替换 %"PRIu32"\n"
           "基因突变 %"PRIu32"\n"
           "插入/删除 %"PRIu32"\n"
           "当前基因型 %u\n"
           "优势型数量 %u\n"
           "事件: %s\n"
           "实验种子 %08"PRIX32,
           s_world.instructions,s_world.births,s_world.replacements,
           s_world.mutations,s_world.insertion_deletions,
           (unsigned)stats.genotype_count,(unsigned)stats.dominant_count,
           event,s_world.seed);
        lv_label_set_text(s_footer,"上/下 切页  长按确定:存档");
    }else{
        lv_obj_add_flag(s_grid,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_detail,LV_OBJ_FLAG_HIDDEN);
        static const char *mutation_text[]={"关闭","1/1024","1/256","1/64"};
        static const char *visual_text[]={"谱系","复制进度","基因长度"};
        lv_label_set_text(s_header,"演化实验控制");
        lv_label_set_text_fmt(s_detail,
            "突变率 %s / 每条复制指令\n"
            "生态视图 %s\n"
            "种子 %08"PRIX32"\n\n"
            "确定：修改突变率\n"
            "长按上：同种子重做\n"
            "长按下：切换生态视图\n"
            "长按确定：新实验\n\n"
            "自动存档：每10分钟\n"
            "存储状态：%s",
            mutation_text[s_world.mutation_level],
            visual_text[s_visual_mode],s_world.seed,
            s_partition?"可用":"不可用");
        lv_label_set_text(s_footer,"上/下 切页  暂停立即存档");
    }
}

static void refresh_locked(void)
{
    if(bsp_lvgl_lock(200)){
        refresh_ui();
        bsp_lvgl_unlock();
    }
}

// Shared button esp_timer callback may only post an event.
static void input_cb(bsp_btn_t key,bsp_btn_ev_t event,void *unused)
{
    (void)unused;
    if(!s_input || (event!=BSP_BTN_CLICK&&event!=BSP_BTN_LONG))return;
    key_msg_t input={key,event};
    (void)xQueueSend(s_input,&input,0);
}
static void handle_key(key_msg_t input)
{
    bool save=false;
    if(input.event==BSP_BTN_CLICK) {
        if(input.key==BSP_BTN_UP)
            s_page=(uint8_t)((s_page+3u)%4u);
        else if(input.key==BSP_BTN_DOWN)
            s_page=(uint8_t)((s_page+1u)%4u);
        else if(input.key==BSP_BTN_OK) {
            if(s_page==0){ s_world.paused^=1u; save=true; }
            else if(s_page==1)
                s_focus=choose_live((uint16_t)((s_focus+1u)%AV_SITES));
            else if(s_page==2) s_focus=choose_live(s_focus);
            else {s_world.mutation_level=(s_world.mutation_level+1u)%4u;save=true;}
        }
    }else if(input.event==BSP_BTN_LONG) {
        if(s_page==3&&input.key==BSP_BTN_UP) {
            uint32_t seed=s_world.seed;
            av_reset(&s_world,seed);s_focus=0;save=true;
        }else if(s_page==3&&input.key==BSP_BTN_DOWN){
            s_visual_mode=(s_visual_mode+1u)%3u;
        }else if(s_page==3&&input.key==BSP_BTN_OK){
            av_reset(&s_world,esp_random());s_focus=0;save=true;
        }else if(input.key==BSP_BTN_OK)save=true;
    }
    if(save)(void)persistence_write();
    bsp_display_backlight(65);
    refresh_locked();
}

// Scientific measurements are independent of LVGL and stream as CSV over
// USB Serial/JTAG; users can capture them without a separate cloud account.
static void export_measurement(void)
{
    av_stats_t s;
    av_stats(&s_world, &s);
    printf("AVIDA_CSV,%08"PRIX32",%"PRIu32",%"PRIu32
           ",%u,%u,%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32
           ",%u,%u\\n",
           s_world.seed, s_world.updates, s_world.instructions,
           (unsigned)s.population, (unsigned)s.genotype_count,
           s_world.births, s_world.replacements, s_world.mutations,
           s_world.insertion_deletions, (unsigned)s.dominant_count,
           (unsigned)s.genotype_length);
    fflush(stdout);
}

static void simulation_task(void *unused)
{
    (void)unused;
    key_msg_t input;
    TickType_t last_input=xTaskGetTickCount();
    for(;;){
        if(xQueueReceive(s_input,&input,
                         pdMS_TO_TICKS(TICK_MILLISECONDS))==pdTRUE){
            handle_key(input);
            last_input=xTaskGetTickCount();
        }else{
            if(bsp_lvgl_lock(200)){
                av_step(&s_world,INSTRUCTIONS_PER_TICK);
                refresh_ui();
                bsp_lvgl_unlock();
            }
            uint32_t now=(uint32_t)(esp_timer_get_time()/1000);
            if(now-s_last_export_ms>=20000u) {
                export_measurement();
                s_last_export_ms=now;
            }
            if(!s_world.paused&&now-s_last_save_ms>=AUTOSAVE_MILLISECONDS)
                (void)persistence_write();
            TickType_t idle=xTaskGetTickCount()-last_input;
            if(idle>pdMS_TO_TICKS(120000))bsp_display_backlight(8);
            else if(idle>pdMS_TO_TICKS(45000))bsp_display_backlight(25);
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG,"数字生命 V2：基于可执行指令的 Avida HEADS 模拟");
    printf("AVIDA_CSV,seed,updates,instructions,population,genotypes,"
           "births,replacements,copy_mutations,indels,dominant_count,"
           "dominant_length\\n");
    (void)bsp_i2c_init();
    (void)bsp_battery_init();
    init_experiment();
    if(bsp_display_init()!=ESP_OK||!bsp_lvgl_init()){
        ESP_LOGE(TAG,"显示初始化失败 MOSI=%d SCLK=%d",
                 BSP_LCD_MOSI,BSP_LCD_SCLK);
        return;
    }
    bsp_display_backlight(65);
    if(!bsp_lvgl_lock(1500))return;
    create_ui();
    refresh_ui();
    bsp_lvgl_unlock();

    s_input=xQueueCreate(12,sizeof(key_msg_t));
    if(!s_input){ESP_LOGE(TAG,"创建输入队列失败");return;}
    if(xTaskCreate(simulation_task,"avida_v2",6144,NULL,5,NULL)!=pdPASS){
        ESP_LOGE(TAG,"创建生命任务失败");
        vQueueDelete(s_input);s_input=NULL;
        return;
    }
    if(bsp_button_init(input_cb,NULL)!=ESP_OK)
        ESP_LOGE(TAG,"按键初始化失败，模拟仍继续");
}
