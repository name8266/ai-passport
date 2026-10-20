#include "brake_app.h"
#include "brake_sound.h"
#include "brake_assets.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "brake_display_math.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stdatomic.h>

static const char *TAG="brake_hero";
typedef struct { bsp_btn_t key; bsp_btn_ev_t event; int64_t at; } input_t;
typedef struct { bh_scores_t scores; uint32_t session; } save_t;
static bh_app_t app;
static SemaphoreHandle_t state_mutex;
static QueueHandle_t inputs, saves;
static atomic_uint dropped_inputs;
static bool nvs_ready;
static uint8_t *frame;
static SemaphoreHandle_t flush_done;
static uint16_t *dma_rows;
static uint16_t wire_palette[256];
#define DISPLAY_LINES 40
static atomic_uint refreshes;
static atomic_uint frame_hist[128];
static atomic_uint max_refresh_us;
static int64_t last_refresh;

static void key_callback(bsp_btn_t key,bsp_btn_ev_t event,void *user) {
    (void)user;
    if(event!=BSP_BTN_PRESS && event!=BSP_BTN_RELEASE)return;
    const input_t input={key,event,esp_timer_get_time()};
    if(xQueueSend(inputs,&input,0)!=pdTRUE)atomic_fetch_add(&dropped_inputs,1);
}
static void audio_events(uint32_t ev,const bh_state_t *s) {
    if(ev&BH_EV_START) {bh_sound_new_round();bh_sound_post(SND_START,false);}
    if(ev&BH_EV_100)bh_sound_post(SND_100,false);
    if(ev&BH_EV_TENT)bh_sound_post(SND_TENT,false);
    if(ev&BH_EV_WARNING)bh_sound_post(SND_WARN,true);
    if(ev&BH_EV_NEAR)bh_sound_post(SND_NEAR,false);
    if(ev&BH_EV_BREAK)bh_sound_post(SND_BREAK,true);
    if(ev&BH_EV_END) {
        if(s->end==BH_STOPPED)bh_sound_post(SND_WIN,true);
        else {
            bh_sound_post((bh_sound_t)(SND_WALL+s->obstacle),true);
            bh_sound_post(SND_LOSE,false);
        }
    }
}
static void game_task(void *arg) {
    (void)arg;
    uint32_t saved_session=0;
    for(;;) {
        input_t in;uint32_t events=0;
        xSemaphoreTake(state_mutex,portMAX_DELAY);
        unsigned old_session=app.session;
        while(xQueueReceive(inputs,&in,0)==pdTRUE) {
            // Late callbacks cannot rewind the monotonic physics clock.
            int64_t at=in.at<app.clock.time_us?app.clock.time_us:in.at;
            events|=bh_app_input(&app,(bh_key_t)in.key,in.event==BSP_BTN_PRESS,at);
        }
        events|=bh_app_sync(&app,bsp_button_pressed_mask(),esp_timer_get_time());
        if(old_session!=app.session) {
            app.view.car.obstacle=esp_random()%4;
            bh_sound_new_round();
        }
        if(app.finished_session && app.finished_session!=saved_session) {
            saved_session=app.finished_session;
            if(app.view.rank>=0) {
                const save_t job={app.view.scores,app.finished_session};
                xQueueOverwrite(saves,&job);
            }
        }
        const bh_state_t car=app.view.car;
        xSemaphoreGive(state_mutex);
        audio_events(events,&car);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
static bool save_scores(const bh_scores_t *scores) {
    if(!nvs_ready)return false;
    nvs_handle_t h;
    esp_err_t e=nvs_open("brake_hero",NVS_READWRITE,&h);
    if(e!=ESP_OK)return false;
    uint8_t bytes[BH_SCORE_BYTES];bh_scores_encode(scores,bytes);
    e=nvs_set_blob(h,"top10_v1",bytes,sizeof(bytes));
    if(e==ESP_OK)e=nvs_commit(h);
    nvs_close(h);
    if(e!=ESP_OK)ESP_LOGW(TAG,"Score write failed: %s",esp_err_to_name(e));
    return e==ESP_OK;
}
static void storage_task(void *arg) {
    (void)arg;
    save_t job;
    int64_t next_battery=0;
    for(;;) {
        if(xQueueReceive(saves,&job,pdMS_TO_TICKS(100))==pdTRUE) {
            bool ok=save_scores(&job.scores);
            xSemaphoreTake(state_mutex,portMAX_DELAY);
            app.view.storage_ok=ok;
            if(app.session==job.session)app.view.saved=ok;
            xSemaphoreGive(state_mutex);
        }
        if(esp_timer_get_time()>=next_battery) {
            int battery=bsp_battery_soc();next_battery=esp_timer_get_time()+10000000;
            xSemaphoreTake(state_mutex,portMAX_DELAY);app.view.battery=battery;xSemaphoreGive(state_mutex);
        }
    }
}
static void load_scores(void) {
    esp_err_t e=nvs_flash_init();
    // Never erase the board's NVS to recover this application's records.
    if(e!=ESP_OK){ESP_LOGW(TAG,"NVS unavailable (%s); volatile scores",esp_err_to_name(e));return;}
    nvs_ready=true;app.view.storage_ok=true;
    nvs_handle_t h;
    if(nvs_open("brake_hero",NVS_READONLY,&h)!=ESP_OK)return;
    uint8_t bytes[BH_SCORE_BYTES];size_t size=sizeof(bytes);
    e=nvs_get_blob(h,"top10_v1",bytes,&size);nvs_close(h);
    if(e==ESP_OK && !bh_scores_decode(&app.view.scores,bytes,size))
        ESP_LOGW(TAG,"Score version/CRC invalid; recovered an empty board");
    else if(e!=ESP_OK && e!=ESP_ERR_NVS_NOT_FOUND)ESP_LOGW(TAG,"Score read failed: %s",esp_err_to_name(e));
}
static void refresh_ready(void) {
    const int64_t now=esp_timer_get_time();
    if(last_refresh) {
        unsigned us=(unsigned)(now-last_refresh),bin=us/1000;
        if(bin>127)bin=127;
        atomic_fetch_add(&frame_hist[bin],1);
        if(us>atomic_load(&max_refresh_us))atomic_store(&max_refresh_us,us);
    }
    last_refresh=now;atomic_fetch_add(&refreshes,1);
}
static bool transfer_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *event, void *user) {
    (void)io;(void)event;(void)user;
    BaseType_t woken=pdFALSE;
    xSemaphoreGiveFromISR(flush_done,&woken);
    return woken==pdTRUE;
}
static bool flush_frame(void) {
    for(int y=0;y<BH_H;y+=DISPLAY_LINES) {
        bh_convert_rows(dma_rows,frame,y,DISPLAY_LINES,wire_palette);
        esp_err_t e=esp_lcd_panel_draw_bitmap(bsp_display_panel(),0,y,BH_W,
                                             y+DISPLAY_LINES,dma_rows);
        if(e!=ESP_OK || xSemaphoreTake(flush_done,pdMS_TO_TICKS(150))!=pdTRUE) {
            ESP_LOGE(TAG,"Display transfer failed (%s); stopping renderer safely",esp_err_to_name(e));
            return false;
        }
    }
    refresh_ready();
    return true;
}
static void render_task(void *arg) {
    (void)arg;
    uint32_t frames=0,render_max=0,render_total=0;
    int64_t window=esp_timer_get_time(),deadline=window;
    for(;;) {
        bh_view_t view;
        xSemaphoreTake(state_mutex,portMAX_DELAY);view=app.view;xSemaphoreGive(state_mutex);
        int64_t start=esp_timer_get_time();
        /* This task owns both buffers. It cannot overwrite DMA rows until the
         * completion semaphore arrives. Audio/logic run during every SPI wait. */
        bh_render(frame,&view);
        if(!flush_frame()){vTaskDelete(NULL);return;}
        uint32_t elapsed=(uint32_t)(esp_timer_get_time()-start);
        render_total+=elapsed;if(elapsed>render_max)render_max=elapsed;++frames;
        int64_t now=esp_timer_get_time();
        if(now-window>=10000000) {
            unsigned actual=atomic_exchange(&refreshes,0),samples=0,bins[128];
            for(int i=0;i<128;++i){bins[i]=atomic_exchange(&frame_hist[i],0);samples+=bins[i];}
            unsigned cumulative=0,p95=0;
            for(int i=0;i<128;++i){cumulative+=bins[i];if(cumulative*100>=samples*95){p95=i+1;break;}}
            ESP_LOGI(TAG,"PERF refresh_fps=%.2f submitted_fps=%.2f interval_p95_ms<=%u interval_max_us=%u render_mean_us=%u render_max_us=%u internal_min=%u dma_free=%u dma_largest=%u framebuffer=%u lcd_dma=19200 audio_max_gap_us=%u dropped_input=%u",
                actual*1000000.0/(now-window),frames*1000000.0/(now-window),p95,
                atomic_exchange(&max_refresh_us,0),frames?render_total/frames:0,render_max,
                (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA),BH_FRAME_BYTES,
                bh_sound_max_gap_us(),atomic_load(&dropped_inputs));
            frames=render_total=render_max=0;window=now;
        }
        deadline+=33333;
        int64_t left=deadline-esp_timer_get_time();
        if(left>1000)vTaskDelay(pdMS_TO_TICKS((unsigned)(left/1000)));
        else {deadline=esp_timer_get_time();vTaskDelay(1);}
    }
}
void brake_hero_start(void) {
    bh_app_init(&app,esp_timer_get_time());
    state_mutex=xSemaphoreCreateMutex();inputs=xQueueCreate(24,sizeof(input_t));saves=xQueueCreate(1,sizeof(save_t));
    frame=heap_caps_malloc(BH_FRAME_BYTES,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    dma_rows=heap_caps_malloc(BH_W*DISPLAY_LINES*sizeof(uint16_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    flush_done=xSemaphoreCreateBinary();
    if(!state_mutex||!inputs||!saves||!frame||!dma_rows||!flush_done) {ESP_LOGE(TAG,"Startup allocation failed");return;}
    (void)bsp_i2c_init();load_scores();
    if(bsp_display_init()!=ESP_OK) {ESP_LOGE(TAG,"Display unavailable");return;}
    (void)bsp_battery_init();
    app.view.buttons_ok=bsp_button_init(key_callback,NULL)==ESP_OK;
    if(!app.view.buttons_ok)ESP_LOGE(TAG,"Buttons unavailable");
    app.view.audio_ok=bh_sound_init();
    bh_rgb565_palette(wire_palette,bh_palette);
    const esp_lcd_panel_io_callbacks_t callbacks={.on_color_trans_done=transfer_done};
    if(esp_lcd_panel_io_register_event_callbacks(bsp_display_io(),&callbacks,NULL)!=ESP_OK) {
        ESP_LOGE(TAG,"Display callback registration failed");return;
    }
    bsp_display_backlight(90);
    // Application-lifetime workers; the renderer owns the panel, not LVGL.
    if(xTaskCreate(storage_task,"brake_store",4096,NULL,1,NULL)!=pdPASS ||
       xTaskCreate(game_task,"brake_logic",4096,NULL,6,NULL)!=pdPASS ||
       xTaskCreate(render_task,"brake_render",6144,NULL,2,NULL)!=pdPASS)
        ESP_LOGE(TAG,"Worker allocation failed; restart required");
    ESP_LOGI(TAG,"Brake Hero ready; fictional model, single ADC key only; third-person V800 game mesh; DMA display; audio priority 5; device measurements required");
}
