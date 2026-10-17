#include "coin_pusher_app.h"
#include "pusher_physics.h"
#include "pusher_render.h"
#include "pusher_fx.h"
#include "pusher_store.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

static const char *TAG="coin3d";
static cp_game_t s_game;
static cp_records_t s_records;
static float s_draft_clock;
static uint16_t *s_pixels,*s_pixels2,*s_depth;
static QueueHandle_t s_inputs;
static SemaphoreHandle_t s_flushed;
static bool s_shot,s_dirty,s_frame_changed;
static volatile bool s_usb_capture;
static unsigned s_play_frames,s_play_slow,s_play_interval_peak;static int64_t s_play_elapsed;
static unsigned s_dense_frames,s_dense_interval_peak;static int64_t s_dense_elapsed;
static unsigned s_frames,s_frame_peak,s_physics_peak,s_flush_peak,s_draw_peak,s_prepare_peak,s_raster_peak,s_swap_peak,s_dma_peak;
static float s_test_left,s_burst_clock;
static unsigned s_burst;
static bool s_autorun;static unsigned s_run_left;static float s_run_pause;
static int64_t s_metrics_start;
typedef enum { IN_KEY,IN_DROP,IN_PAUSE,IN_AUTO,IN_RESET,IN_SHOT,IN_STATUS,IN_TEST,IN_EVENT,IN_RELIC,IN_WALLET,IN_COINS,IN_FINISH,IN_CAST,IN_AUTORUN,IN_BENCH } action_t;
typedef struct { action_t action;int key,event,arg; } input_t;

/* A disconnected diagnostics port must never block the game owner. */
static int log_to_usb(const char *format,va_list args){
    if(s_usb_capture)return 0;
    char line[512];int length=vsnprintf(line,sizeof(line),format,args);
    if(length>0){size_t n=(size_t)length;if(n>=sizeof(line))n=sizeof(line)-1;(void)usb_serial_jtag_write_bytes(line,n,0);}
    return length;
}
static bool IRAM_ATTR on_flush(esp_lcd_panel_io_handle_t io,esp_lcd_panel_io_event_data_t *ev,void *ctx)
{
    (void)io;(void)ev;BaseType_t wake=pdFALSE;
    xSemaphoreGiveFromISR((SemaphoreHandle_t)ctx,&wake);
    /* IDF's SPI LCD post callback ignores the boolean callback result. */
    portYIELD_FROM_ISR(wake);return false;
}
static void on_key(bsp_btn_t button,bsp_btn_ev_t event,void *ctx)
{
    (void)ctx;input_t msg={IN_KEY,button,event,0};
    if(s_inputs)(void)xQueueSend(s_inputs,&msg,0);
}
static uint32_t clock_us(void){return (uint32_t)esp_timer_get_time();}
static void status(void)
{
    unsigned tilted=0,resting=0;float high=0;
    for(unsigned i=0;i<CP_MAX_COINS;++i) {
        cp_coin_t *b=&s_game.coins[i];
    if(!b->active)continue;
        if(fabsf(cp_body_axis(b,2).z)<.95f)++tilted;
    if(b->sleeping)++resting;
        if(b->p.z>high)high=b->p.z;
    }
    ESP_LOGI(TAG,"STATUS bank=%lu win=%lu paid=%lu payout=%lu side=%lu prizes=%lu bodies=%u tower=%u tilted=%u rest=%u high=%.1f contacts=%u peak=%u overflow=%lu valid=%d time=%.2f steps=%lu",
        (unsigned long)s_game.bank,(unsigned long)s_game.score,(unsigned long)s_game.drops,
        (unsigned long)s_game.collected,(unsigned long)s_game.lost,(unsigned long)s_game.prizes,
        cp_coin_count(&s_game),cp_tower_count(&s_game),tilted,resting,(double)high,s_game.contacts,
        s_game.peak_contacts,(unsigned long)s_game.overflow_steps,cp_valid(&s_game),
        (double)s_game.game_time,(unsigned long)s_game.steps);
    ESP_LOGI(TAG,"ROGUE view=%u level=%u xp=%u/%u chips=%lu gross=%lu coins=%lu grace=%u build=%u synergy=%02lx seed=%lu practice=%d ranks=%lu generation=%lu saved=%lu save_failed=%d",
        s_game.rogue.view,s_game.rogue.level,s_game.rogue.xp,s_game.rogue.goal,(unsigned long)s_game.score,(unsigned long)s_game.rogue.gross,(unsigned long)s_game.bank,s_game.rogue.grace,
        (unsigned)__builtin_popcount(s_game.rogue.discovered),(unsigned long)s_game.rogue.synergies,(unsigned long)s_game.rogue.seed,s_game.rogue.practice,
        (unsigned long)s_records.rows[0].chips,(unsigned long)s_records.generation,(unsigned long)cp_fx_saved_generation(),cp_fx_save_failed());
    ESP_LOGI(TAG,"DRAFT offers=%u/%u/%u cursor=%u pending=%u rerolls=%u relic=%u rank=%u",s_game.rogue.offer[0],s_game.rogue.offer[1],s_game.rogue.offer[2],s_game.rogue.cursor,s_game.rogue.pending,s_game.rogue.rerolls,s_game.rogue.inspect,s_game.rogue.rank);
    ESP_LOGI(TAG,"TACTICS act=%u target=%u left=%u casts=%lu links=%lu blasts=%lu grown=%lu crown=%d/%d won=%d end=%u stock=%u/%u/%u/%u/%u/%u/%u/%u",s_game.tactics.stage+1,cp_tactics_goal(&s_game),cp_tactics_seconds(&s_game),(unsigned long)s_game.tactics.casts,(unsigned long)s_game.tactics.links,(unsigned long)s_game.tactics.blasts,(unsigned long)s_game.tactics.grown,s_game.tactics.crown_spawned,s_game.tactics.crown_collected,s_game.tactics.won,s_game.tactics.end_reason,s_game.tactics.stock[0],s_game.tactics.stock[1],s_game.tactics.stock[2],s_game.tactics.stock[3],s_game.tactics.stock[4],s_game.tactics.stock[5],s_game.tactics.stock[6],s_game.tactics.stock[7]);
    ESP_LOGI(TAG,"QUEUE pending=%u head=%u house=%lu muted=%d",s_game.tactics.queue_count,cp_tactics_auto_kind(&s_game),(unsigned long)s_game.tactics.house_coins,s_game.rogue.muted);
    ESP_LOGI(TAG,"TACTIC_CASTS types=%u/%u/%u/%u/%u/%u/%u/%u",s_game.tactics.used[0],s_game.tactics.used[1],s_game.tactics.used[2],s_game.tactics.used[3],s_game.tactics.used[4],s_game.tactics.used[5],s_game.tactics.used[6],s_game.tactics.used[7]);
    unsigned gold=0,copper=0;
    for(unsigned i=0;i<CP_MAX_COINS;++i)if(s_game.coins[i].active){gold+=s_game.coins[i].kind==CP_COIN_GOLD;copper+=s_game.coins[i].kind==CP_COIN_COPPER;}
    ESP_LOGI(TAG,"GAME events=%lu jackpot=%lu spins=%lu mask=%u rush=%u save=%u rain=%u bonus=%lu restock=%lu gold=%u copper=%u",(unsigned long)s_game.events,(unsigned long)s_game.jackpots,(unsigned long)s_game.arcade.spins,s_game.collection_mask,s_game.rush_ticks,s_game.guard_ticks,s_game.bonus_queue,(unsigned long)s_game.bonus_drops,(unsigned long)s_game.restocks,gold,copper);
    ESP_LOGI(TAG,"ARCADE spins=%lu gate_hits=%lu lane=%u spin=%u result=%u hole=%u double=%u shake=%u fever=%u siphoned=%lu clones=%lu towers=%lu tower_coins=%lu tray=%u side=%u queue=%u",
        (unsigned long)s_game.arcade.spins,(unsigned long)s_game.arcade.gate_hits,s_game.arcade.lane,s_game.arcade.spin_ticks,s_game.arcade.last_result,
        s_game.arcade.hole_ticks,s_game.arcade.double_ticks,s_game.arcade.shake_ticks,s_game.arcade.fever_ticks,
        (unsigned long)s_game.arcade.siphoned,(unsigned long)s_game.arcade.clones,(unsigned long)s_game.arcade.towers,(unsigned long)s_game.arcade.tower_coins,
        s_game.arcade.tower_ticks,s_game.arcade.tower_side,s_game.arcade.tower_queue);
    ESP_LOGI(TAG,"FLOW circulation=%d cycles=%lu capacity_waits=%lu",s_game.circulation,(unsigned long)s_game.circulation_cycles,(unsigned long)s_game.capacity_waits);
    ESP_LOGI(TAG,"FEED rod=%lu pegs=%lu exits=%lu bins=%lu/%lu/%lu",
        (unsigned long)s_game.rod_hits,(unsigned long)s_game.peg_hits,(unsigned long)s_game.feeder_exits,
        (unsigned long)s_game.exit_bins[0],(unsigned long)s_game.exit_bins[1],(unsigned long)s_game.exit_bins[2]);
    ESP_LOGI(TAG,"STAGE us prep=%lu contacts=%lu solve=%lu finish=%lu ground=%lu pairs=%lu factory=%lu stack=%u sat=%u cached=%u",
        (unsigned long)s_game.prep_us,(unsigned long)s_game.contact_us,(unsigned long)s_game.solve_us,
        (unsigned long)s_game.finish_us,(unsigned long)s_game.ground_us,(unsigned long)s_game.pairs_us,(unsigned long)s_game.factory_us,(unsigned)uxTaskGetStackHighWaterMark(NULL),s_game.sat_calls,s_game.cache_hits);
}
static void cast(unsigned kind){bool ok=cp_tactics_cast(&s_game,kind);if(ok)s_dirty=true;ESP_LOGI(TAG,"SPECIAL kind=%u accepted=%d",kind,ok);}
static void launch(void)
{
    bool accepted=cp_drop(&s_game);
    if(accepted)s_dirty=true;
    ESP_LOGI(TAG,"INPUT fixed-entry drop=%s bank=%lu",accepted?"accepted":"blocked",(unsigned long)s_game.bank);
}
static void new_game_seed(uint32_t seed)
{
    cp_fx_notify(CP_FX_RESET);
    uint32_t best=s_game.best;cp_init(&s_game,seed,CP_RUN_COINS,best,1);cp_rogue_start(&s_game,s_records.discovered);s_game.rogue.muted=cp_fx_muted();
    s_test_left=0;s_draft_clock=0;s_burst=0;s_burst_clock=0;s_dirty=true;ESP_LOGI(TAG,"Fresh physical cabinet: 256-body cabinet, 32 inlet/event reserves, 2 towers, 3 prizes; paid coin chute ready");
}
static void new_game(void){new_game_seed(esp_random());}
static void inputs(void)
{
    input_t m;
    while(xQueueReceive(s_inputs,&m,0)==pdTRUE) {
        switch(m.action) {
        case IN_KEY:
            if(m.event==BSP_BTN_DOUBLE&&m.key==BSP_BTN_OK&&s_game.rogue.view==CP_VIEW_PLAY){
                s_burst=2;s_burst_clock=0;s_dirty=true;
            }else if(m.event==BSP_BTN_CLICK||m.event==BSP_BTN_LONG||m.event==BSP_BTN_DOUBLE){
                cp_rogue_cmd_t cmd=cp_rogue_key(&s_game,(unsigned)m.key,m.event==BSP_BTN_LONG);
                if(cmd==CP_CMD_NEW)new_game();
                else if(cmd==CP_CMD_SOUND){cp_fx_mute(s_game.rogue.muted);ESP_LOGI(TAG,"SETTINGS muted=%d",s_game.rogue.muted);}
                else if(cmd==CP_CMD_DROP)launch();
                else if(cmd==CP_CMD_SPECIAL)cast(s_game.tactics.selected+1);
                else if(cmd==CP_CMD_BURST){s_burst=3;s_burst_clock=0;}
                s_dirty=true;
            }
            break;
        case IN_CAST:cast((unsigned)m.arg);break;
        case IN_AUTORUN:s_autorun=m.arg>0;s_run_left=(unsigned)m.arg;s_run_pause=0;if(!s_autorun)s_game.autoplay=false;if(s_autorun){new_game();s_game.rogue.practice=true;s_game.autoplay=true;}ESP_LOGI(TAG,"AUTORUN rounds=%u practice=1",s_run_left);break;
        case IN_BENCH:s_autorun=false;s_run_left=0;new_game_seed((uint32_t)m.arg);s_game.rogue.practice=true;s_game.autoplay=true;s_test_left=90;ESP_LOGI(TAG,"BENCH seed=%d seconds=90 practice=1",m.arg);break;
        case IN_DROP:launch();break;
        case IN_PAUSE:if(s_game.rogue.view==CP_VIEW_PLAY)cp_rogue_view(&s_game,CP_VIEW_PAUSE);else if(s_game.rogue.view==CP_VIEW_PAUSE)cp_rogue_view(&s_game,CP_VIEW_PLAY);break;
        case IN_AUTO:if(s_game.rogue.view==CP_VIEW_PLAY)s_game.autoplay=!s_game.autoplay;break;
        case IN_RESET:s_autorun=false;s_run_left=0;new_game();break;
        case IN_SHOT:s_shot=true;break;
        case IN_STATUS:status();break;
        case IN_EVENT:s_game.rogue.practice=true;cp_reward_event(&s_game,(cp_event_t)m.arg);s_dirty=true;break;
        case IN_RELIC:s_game.rogue.practice=true;cp_rogue_grant(&s_game,(unsigned)m.arg);s_dirty=true;break;
        case IN_WALLET:s_game.rogue.practice=true;s_game.score=(unsigned)m.arg;s_dirty=true;break;
        case IN_COINS:s_game.rogue.practice=true;s_game.bank=(unsigned)m.arg;s_dirty=true;break;
        case IN_FINISH:cp_rogue_finish(&s_game);s_dirty=true;break;
        case IN_TEST:s_test_left=(float)m.arg;s_game.autoplay=true;ESP_LOGI(TAG,"TEST begin %d seconds; same paid chute and physics path",m.arg);break;
        }
    }
}
static void serial_task(void *ctx)
{
    (void)ctx;
    char line[64];unsigned len=0;
    while(true) {
        char ch;int n=usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(20));
        if(n==1) {
            if(ch=='\n'||ch=='\r') {
                line[len]=0;len=0;input_t m={.action=IN_STATUS};bool known=true;
                if(!strcmp(line,"drop"))m.action=IN_DROP;
                else if(!strcmp(line,"pause"))m.action=IN_PAUSE;
                else if(!strcmp(line,"auto"))m.action=IN_AUTO;
                else if(!strcmp(line,"reset"))m.action=IN_RESET;
                else if(!strcmp(line,"shot"))m.action=IN_SHOT;
                else if(!strncmp(line,"key ",4)){m.action=IN_KEY;m.event=BSP_BTN_CLICK;
                    if(!strcmp(line+4,"up"))m.key=BSP_BTN_UP;else if(!strcmp(line+4,"down"))m.key=BSP_BTN_DOWN;
                    else if(!strcmp(line+4,"ok"))m.key=BSP_BTN_OK;else if(!strcmp(line+4,"double")){m.key=BSP_BTN_OK;m.event=BSP_BTN_DOUBLE;}else if(!strcmp(line+4,"menu")){m.key=BSP_BTN_OK;m.event=BSP_BTN_LONG;}else known=false;}
                else if(!strncmp(line,"relic ",6)){m.action=IN_RELIC;m.arg=atoi(line+6);if(m.arg<0||m.arg>=CP_RELIC_COUNT)known=false;}
                else if(!strncmp(line,"wallet ",7)){m.action=IN_WALLET;m.arg=(int)cp_clamp(atoi(line+7),0,CP_CHIP_LIMIT);}
                else if(!strncmp(line,"coins ",6)){m.action=IN_COINS;m.arg=(int)cp_clamp(atoi(line+6),0,999);}
                else if(!strcmp(line,"finish"))m.action=IN_FINISH;
                else if(!strncmp(line,"cast ",5)){m.action=IN_CAST;m.arg=atoi(line+5);if(m.arg<1||m.arg>CP_SPECIAL_COUNT)known=false;}
                else if(!strncmp(line,"bench ",6)){m.action=IN_BENCH;m.arg=atoi(line+6);if(m.arg<=0)known=false;}
                else if(!strncmp(line,"autorun ",8)){m.action=IN_AUTORUN;m.arg=atoi(line+8);if(m.arg<0||m.arg>30)known=false;}
                else if(!strncmp(line,"event ",6)){m.action=IN_EVENT;m.arg=0;
                    static const char *names[]={"","rush","rain","save","jackpot","hole","double","towers","shake","fever"};
                    for(unsigned k=1;k<=CP_EVENT_MAX;++k)if(!strcmp(line+6,names[k]))m.arg=(int)k;
                    if(!m.arg)known=false;}
                else if(!strncmp(line,"test ",5)){m.action=IN_TEST;m.arg=(int)cp_clamp(atoi(line+5),1,90);}
                else if(strcmp(line,"status"))known=false;
                if(known)(void)xQueueSend(s_inputs,&m,0);
            } else if(len<sizeof(line)-1&&ch>=32&&ch<127)line[len++]=ch;
        } else vTaskDelay(pdMS_TO_TICKS(10));
    }
}
static bool frame_bytes(const uint16_t *tile,size_t size)
{
    const uint8_t *p=(const uint8_t *)tile;
    while(size){size_t n=size>512?512:size;if(usb_serial_jtag_write_bytes(p,n,pdMS_TO_TICKS(500))!=(int)n)return false;p+=n;size-=n;}
    return true;
}
static bool draw(void)
{
    int64_t stamp=esp_timer_get_time();cp_render_prepare(&s_game);unsigned prep=(unsigned)(esp_timer_get_time()-stamp),raster=0,swap=0,dma=0;
    bool shot=s_shot;s_shot=false;int battery=cp_fx_battery();bool frame_dense=cp_coin_count(&s_game)>=250;
    if(shot){s_usb_capture=true;printf("CP3D_FRAME 240 320 RGB565_LE\n");fflush(stdout);}
    static int previous_battery=-2;uint16_t dirty=shot?UINT16_MAX:cp_render_dirty();
    bool battery_changed=previous_battery!=battery;if(battery_changed){dirty|=7;previous_battery=battery;}s_frame_changed=dirty!=0;
    /* Keep the final DMA transfer in flight while the next physics step
     * runs. A persistent bank index always renders into the other buffer. */
    static bool pending,previous_final,previous_countable,previous_play,previous_dense;
    static unsigned bank;
    uint16_t *banks[2]={s_pixels,s_pixels2};
    for(int y=0;y<320;y+=CP_TILE_H) {
        if(!(dirty&(1u<<(y/CP_TILE_H))))continue;
        uint16_t *tile=banks[bank++%2];int x0=0,x1=240,draw_y=y,draw_end=y+CP_TILE_H;bool native=shot;
        if(!shot&&!(battery_changed&&y<CP_TILE_H*3))cp_render_area(y,&x0,&draw_y,&x1,&draw_end);
        stamp=esp_timer_get_time();if(shot)cp_render_tile(&s_game,y,tile,s_depth,battery);else cp_render_spi_area(&s_game,draw_y,draw_end-draw_y,tile,s_depth,battery,x0,x1);raster+=(unsigned)(esp_timer_get_time()-stamp);
        if(shot&&!frame_bytes(tile,240*CP_TILE_H*2)){printf("\nCP3D_FRAME_ABORT\n");shot=false;}
        stamp=esp_timer_get_time();typedef uint32_t alias_u32 __attribute__((__may_alias__));alias_u32 *words=(alias_u32 *)tile;
        if(native){for(unsigned i=0;i<240*CP_TILE_H/2;++i){uint32_t v=words[i];words[i]=((v&0xff00ff00u)>>8)|((v&0x00ff00ffu)<<8);}}swap+=(unsigned)(esp_timer_get_time()-stamp);
        cp_render_pack_area(tile,x0,x1,(unsigned)(draw_end-draw_y));
        int64_t start=esp_timer_get_time();
        stamp=esp_timer_get_time();
    if(pending){
        if(xSemaphoreTake(s_flushed,pdMS_TO_TICKS(500))!=pdTRUE){s_usb_capture=false;ESP_LOGE(TAG,"DMA completion timed out");return false;}
        if(previous_final&&previous_countable){++s_frames;if(previous_play){++s_play_frames;if(previous_dense)++s_dense_frames;}}
    }
        esp_err_t e=esp_lcd_panel_draw_bitmap(bsp_display_panel(),x0,draw_y,x1,draw_end,tile);
        if(e!=ESP_OK){s_usb_capture=false;ESP_LOGE(TAG,"Display transfer failed: %s",esp_err_to_name(e));return false;}
        unsigned elapsed=(unsigned)(esp_timer_get_time()-start);
        if(elapsed>s_flush_peak)s_flush_peak=elapsed;
        dma+=elapsed;pending=true;
        previous_final=(dirty&((1u<<(CP_SCREEN_H/CP_TILE_H))-1u))>>(y/CP_TILE_H+1)==0;previous_countable=!shot;previous_play=!s_game.paused;previous_dense=frame_dense;
    }
    stamp=esp_timer_get_time();
    if(pending&&(shot||!dirty)){
        if(xSemaphoreTake(s_flushed,pdMS_TO_TICKS(500))!=pdTRUE){s_usb_capture=false;ESP_LOGE(TAG,"Final DMA completion timed out");return false;}
        if(previous_final&&previous_countable){++s_frames;if(previous_play){++s_play_frames;if(previous_dense)++s_dense_frames;}}
        pending=false;
    }
    dma+=(unsigned)(esp_timer_get_time()-stamp);if(prep>s_prepare_peak)s_prepare_peak=prep;if(raster>s_raster_peak)s_raster_peak=raster;if(swap>s_swap_peak)s_swap_peak=swap;if(dma>s_dma_peak)s_dma_peak=dma;
    if(shot){printf("\nCP3D_FRAME_END\n");fflush(stdout);}
    s_usb_capture=false;return true;
}
static void game_task(void *ctx)
{
    (void)ctx;int64_t previous=esp_timer_get_time();
    uint32_t payout=s_game.collected,prizes=s_game.prizes,events=s_game.events,restocks=s_game.restocks,jackpots=s_game.jackpots,spins=s_game.arcade.spins,towers=s_game.arcade.towers;
    bool spinning=false,previous_active=false,previous_dense=false;uint32_t links=0,blasts=0;
    s_metrics_start=previous;
    while(true) {
        int64_t start=esp_timer_get_time();inputs();
        int64_t interval=start-previous;
        if(previous_active){s_play_elapsed+=interval;if(interval>40000)++s_play_slow;if(interval>s_play_interval_peak)s_play_interval_peak=(unsigned)interval;}
        if(previous_dense){s_dense_elapsed+=interval;if(interval>s_dense_interval_peak)s_dense_interval_peak=(unsigned)interval;}
        float elapsed=(float)interval/1000000.0f;previous=start;
        if(s_test_left>0) {
            s_test_left-=elapsed;
            if(s_test_left<=0){s_game.autoplay=false;ESP_LOGI(TAG,"TEST end");status();}
        }
        if((s_test_left>0||s_autorun)&&(s_game.rogue.view==CP_VIEW_DRAFT||s_game.rogue.view==CP_VIEW_SUPPLY)){s_draft_clock+=elapsed;
            if(s_draft_clock>=.5f){
                if(s_game.rogue.view==CP_VIEW_SUPPLY)cp_tactics_supply(&s_game,s_game.rogue.cursor);
                else{unsigned best=0,weight=0;for(unsigned k=0;k<3;++k){unsigned id=s_game.rogue.offer[k],w=s_game.rogue.relic[id]*3+((s_game.rogue.seed>>(id%16))&3);if(w>weight){weight=w;best=k;}}cp_rogue_choose(&s_game,best);}
                s_draft_clock=0;s_dirty=true;}}
        else s_draft_clock=0;
        if(s_burst&&!s_game.paused) {
            s_burst_clock-=elapsed;
            if(s_burst_clock<=0) {if(cp_drop(&s_game)){--s_burst;s_dirty=true;}s_burst_clock=.35f;}
        }
        if(s_autorun&&!s_game.rogue.finished&&s_game.bank<=3)(void)cp_rogue_buy(&s_game);
        int64_t phy=esp_timer_get_time();cp_step(&s_game,elapsed);
        unsigned phy_us=(unsigned)(esp_timer_get_time()-phy);
    if(phy_us>s_physics_peak)s_physics_peak=phy_us;
        if(!cp_valid(&s_game)){ESP_LOGE(TAG,"Invalid rigid-body state; simulation stopped");s_game.paused=true;status();}
        if(s_game.collected!=payout||s_game.prizes!=prizes) {
            if(s_game.collected>payout)cp_fx_notify(s_game.prizes>prizes?CP_FX_PRIZE:CP_FX_COIN);
            payout=s_game.collected;prizes=s_game.prizes;s_dirty=true;
            ESP_LOGI(TAG,"PAYOUT coins/items=%lu prizes=%lu bank=%lu",(unsigned long)payout,(unsigned long)prizes,(unsigned long)s_game.bank);
        }
        if(links!=s_game.tactics.links||blasts!=s_game.tactics.blasts){if(s_game.tactics.links>links||s_game.tactics.blasts>blasts)cp_fx_notify(CP_FX_EVENT);links=s_game.tactics.links;blasts=s_game.tactics.blasts;}
        if(spins!=s_game.arcade.spins){if(s_game.arcade.spins>spins){cp_fx_notify(CP_FX_SPIN);ESP_LOGI(TAG,"REELS start n=%lu lane=%u",(unsigned long)s_game.arcade.spins,s_game.arcade.lane);}spins=s_game.arcade.spins;}
        if(spinning&&!s_game.arcade.spin_ticks&&s_game.arcade.result)ESP_LOGI(TAG,"REELS resolved n=%lu result=%u",(unsigned long)s_game.arcade.spins,s_game.arcade.result);
        spinning=s_game.arcade.spin_ticks>0;
        if(towers!=s_game.arcade.towers){if(s_game.arcade.towers>towers)cp_fx_notify(CP_FX_PRIZE);towers=s_game.arcade.towers;}
        if(jackpots!=s_game.jackpots){if(s_game.jackpots>jackpots)cp_fx_notify(CP_FX_JACKPOT);jackpots=s_game.jackpots;}
        if(events!=s_game.events||restocks!=s_game.restocks){
            if(s_game.events>events&&s_game.arcade.last_result!=CP_EVENT_JACKPOT)cp_fx_notify(s_game.arcade.last_result==CP_EVENT_BLACKHOLE?CP_FX_HOLE:CP_FX_EVENT);
            events=s_game.events;restocks=s_game.restocks;s_dirty=true;
            ESP_LOGI(TAG,"REWARD event=%u events=%lu jackpot=%lu restocks=%lu bonus=%lu queued=%u",s_game.event_kind,(unsigned long)events,(unsigned long)s_game.jackpots,(unsigned long)restocks,(unsigned long)s_game.bonus_drops,s_game.bonus_queue);
        }
        s_game.rogue.store_failed=cp_fx_save_failed();
        if(s_game.rogue.discovered!=s_records.discovered&&!s_game.rogue.practice){
            s_records.discovered|=s_game.rogue.discovered;++s_records.generation;cp_records_seal(&s_records);cp_fx_store(&s_records);cp_fx_notify(CP_FX_EVENT);
        }
        if(s_game.rogue.finished&&!s_game.rogue.recorded){
            bool ranked=cp_records_add(&s_records,&s_game);s_game.rogue.recorded=true;
            if(!s_game.rogue.practice)cp_fx_store(&s_records);
            ESP_LOGI(TAG,"RUN end ranked=%d practice=%d chips=%lu level=%u paid=%lu seed=%lu",ranked,s_game.rogue.practice,(unsigned long)s_game.score,s_game.rogue.level,(unsigned long)s_game.drops,(unsigned long)s_game.rogue.seed);
        }
        if(s_autorun&&s_game.rogue.recorded){s_run_pause+=elapsed;if(s_run_pause>=2){
            ESP_LOGI(TAG,"AUTORUN result remaining=%u won=%d stage=%u casts=%lu links=%lu grown=%lu",s_run_left,s_game.tactics.won,s_game.tactics.stage+1,(unsigned long)s_game.tactics.casts,(unsigned long)s_game.tactics.links,(unsigned long)s_game.tactics.grown);status();
            if(s_run_left>1){--s_run_left;new_game();s_game.rogue.practice=true;s_game.autoplay=true;s_run_pause=0;}
            else{s_autorun=false;s_run_left=0;ESP_LOGI(TAG,"AUTORUN complete");}
        }}
        previous_active=!s_game.paused;previous_dense=previous_active&&cp_coin_count(&s_game)>=250;
        bool shot=s_shot;
        int64_t draw_start=esp_timer_get_time();
        if(!draw()){vTaskDelete(NULL);return;}
        unsigned draw_us=(unsigned)(esp_timer_get_time()-draw_start);if(!shot&&draw_us>s_draw_peak)s_draw_peak=draw_us;
        if(shot){int64_t captured=esp_timer_get_time();s_metrics_start+=captured-start;previous=captured;} /* Exclude deliberate capture pause from frame rate. */

        int64_t now=esp_timer_get_time();
        now=esp_timer_get_time();unsigned frame_us=(unsigned)(now-start);if(!shot&&frame_us>s_frame_peak)s_frame_peak=frame_us;
        if(now-s_metrics_start>10000000) {
            ESP_LOGI(TAG,"FRAME %.1f Hz displayed updates; frame_max=%u physics_batch_max=%u flush_tile_max=%u draw_max=%u us heap=%u min=%u largest=%u",
                (double)s_frames*1000000/(double)(now-s_metrics_start),s_frame_peak,s_physics_peak,s_flush_peak,s_draw_peak,
                (unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            if(s_play_elapsed>1000000)ESP_LOGI(TAG,"PLAY_FRAME %.1f Hz active_s=%.2f intervals_over40ms=%u interval_max_us=%u",(double)s_play_frames*1000000/(double)s_play_elapsed,(double)s_play_elapsed/1000000,s_play_slow,s_play_interval_peak);
            if(s_dense_elapsed>0)ESP_LOGI(TAG,"DENSE_FRAME %.1f Hz bodies>=250 active_s=%.3f presented=%u interval_max_us=%u",(double)s_dense_frames*1000000/(double)s_dense_elapsed,(double)s_dense_elapsed/1000000,s_dense_frames,s_dense_interval_peak);
            s_dense_frames=s_dense_interval_peak=0;s_dense_elapsed=0;
            s_play_frames=s_play_slow=s_play_interval_peak=0;s_play_elapsed=0;
            cp_render_profile_t rp=cp_render_profile();
            ESP_LOGI(TAG,"RASTER last us base=%lu faces=%lu expand=%lu hud=%lu scene=%lu coins=%lu",(unsigned long)rp.base_us,(unsigned long)rp.faces_us,(unsigned long)rp.expand_us,(unsigned long)rp.hud_us,(unsigned long)rp.scene_us,(unsigned long)rp.coins_us);
            ESP_LOGI(TAG,"DRAW us prepare=%u raster=%u swap=%u dma=%u",s_prepare_peak,s_raster_peak,s_swap_peak,s_dma_peak);s_prepare_peak=s_raster_peak=s_swap_peak=s_dma_peak=0;
            status();s_metrics_start=now;s_frames=0;s_frame_peak=s_physics_peak=s_flush_peak=s_draw_peak=0;
        }
        int64_t remaining=33333-(esp_timer_get_time()-start);
        vTaskDelay(pdMS_TO_TICKS(remaining>0?(remaining+999)/1000:1));
    }
}
void coin_pusher_start(void)
{
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=2048,.rx_buffer_size=512};
    if(!usb_serial_jtag_is_driver_installed()&&usb_serial_jtag_driver_install(&usb)!=ESP_OK){ESP_LOGE(TAG,"USB console driver allocation failed");return;}
    esp_log_set_vprintf(log_to_usb);
    usb_serial_jtag_vfs_use_driver();usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    if(!cp_render_assets_ready()){ESP_LOGE(TAG,"Coin material archive missing/mismatched");return;}
    cp_set_clock(clock_us);
    cp_fx_mute(cp_store_muted());
    (void)cp_store_load(&s_records);cp_render_records(&s_records);
    cp_init(&s_game,esp_random(),CP_RUN_COINS,s_records.rows[0].chips,1);cp_rogue_start(&s_game,s_records.discovered);s_game.rogue.muted=cp_fx_muted();
    ESP_LOGI(TAG,"Fresh game on every boot; no round snapshot is loaded");
    s_pixels=heap_caps_malloc(240*CP_TILE_H*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    s_pixels2=heap_caps_malloc(240*CP_TILE_H*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    s_depth=cp_frame_depth_scratch(240*CP_TILE_H*2);
    s_inputs=xQueueCreate(24,sizeof(input_t));s_flushed=xSemaphoreCreateBinary();
    if(!s_pixels||!s_pixels2||!s_depth||!s_inputs||!s_flushed){ESP_LOGE(TAG,"No memory for bounded renderer/tasks");return;}
    bsp_i2c_init();(void)bsp_battery_init();
    if(bsp_display_init()!=ESP_OK)return;
    esp_lcd_panel_io_callbacks_t cb={.on_color_trans_done=on_flush};
    if(esp_lcd_panel_io_register_event_callbacks(bsp_display_io(),&cb,s_flushed)!=ESP_OK)return;
    bsp_display_backlight(85);
    if(bsp_button_init(on_key,NULL)!=ESP_OK)return;
    cp_fx_start();
    if(xTaskCreate(serial_task,"coin3d_usb",3072,NULL,3,NULL)!=pdPASS||
       xTaskCreate(game_task,"coin3d",4608,NULL,5,NULL)!=pdPASS){ESP_LOGE(TAG,"Task creation failed");return;}
    ESP_LOGI(TAG,"TACTICAL PUSHER: 8 physical special coins / 5 acts / automatic special queue / dense cabinet / crown finale; 28 stackable relics, 40 initial coins, chips, local top10; 30 Hz physics, 256 initial bodies / 288 capacity, native 240x320 scene and HUD, 2 decks, 2 towers, refilling physical prizes; lit-lane reels / side towers / black hole / cloning / Chinese UI; fixed entry / swing rod / 13 pegs / front camera; commands status/drop/pause/auto/test N/shot/reset/event NAME");
}
