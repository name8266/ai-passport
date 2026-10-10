#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "../firmware/main/hub_sound.c"
static jmp_buf exit_worker;
static void (*saved_task)(void *);
static uint32_t queued,stored;
static int starts,writes,cleanups,commits,save_error,write_error;
static bool exhausted;
static int64_t clock_us=1000000;
int nvs_open(const char *name,int mode,nvs_handle_t *h) {(void)name;(void)mode;*h=1;return 0;}
void nvs_close(nvs_handle_t h) {(void)h;}
int nvs_get_u32(nvs_handle_t h,const char *k,uint32_t *v) {(void)h;(void)k;*v=stored;return stored?0:1;}
int nvs_set_u32(nvs_handle_t h,const char *k,uint32_t v) {(void)h;(void)k;if(save_error)return 1;stored=v;return 0;}
int nvs_get_u8(nvs_handle_t h,const char *k,uint8_t *v) {(void)h;(void)k;(void)v;return 1;}
int nvs_set_u8(nvs_handle_t h,const char *k,uint8_t v) {(void)h;(void)k;(void)v;return 0;}
int nvs_commit(nvs_handle_t h) {(void)h;commits++;return save_error;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *h) {
    (void)name;(void)stack;(void)arg;(void)priority;saved_task=fn;*h=(void *)1;return pdPASS;
}
int xTaskNotify(TaskHandle_t h,uint32_t bits,int action) {(void)h;(void)action;queued|=bits;return 1;}
int xTaskNotifyWait(uint32_t clear,uint32_t end,uint32_t *bits,uint32_t wait) {
    (void)clear;(void)end;(void)wait;if(exhausted)longjmp(exit_worker,1);exhausted=true;*bits=queued;queued=0;return 1;
}
void vTaskDelay(uint32_t ticks) {(void)ticks;}
int64_t esp_timer_get_time(void) {return clock_us;}
esp_err_t bsp_audio_init(void) {starts++;return 0;}
esp_err_t bsp_audio_set_format(uint32_t rate,uint8_t bits,uint8_t channels) {assert(rate==16000 && bits==16 && channels==1);return 0;}
void bsp_audio_set_volume(uint8_t volume) {assert(volume>=10 && volume<=60);}
esp_err_t bsp_audio_write(const void *pcm,size_t bytes) {(void)pcm;assert(bytes<=320);writes++;return write_error;}
esp_err_t bsp_audio_deinit(void) {cleanups++;return 0;}
const char *esp_err_to_name(int e) {(void)e;return "fault";}
static void drain(void) {exhausted=false;if(setjmp(exit_worker)==0)saved_task(NULL);}
int main(void) {
    assert(hub_sound_start());assert(!hub_sound_get().enabled && worker==NULL);
    hub_sound_config_t initial=hub_sound_get();
    initial.enabled=true;
    hub_sound_set(initial); /* start audio worker lazily on opt-in */
    assert(worker!=NULL);
    drain(); /* persist opt-in before simulating alerts */
    assert(hub_sound_get().enabled);
    hub_sound_notify(1,0);assert(!queued);
    hub_sound_notify(0,4);assert(!queued);
    hub_sound_notify(0,0);drain();assert(starts==1 && writes>0 && cleanups==1);
    hub_sound_config_t c=hub_sound_get();c.enabled=false;
    hub_sound_notify(0,0);hub_sound_set(c);drain();assert(starts==1 && commits==2);
    hub_sound_preview();assert(!queued);
    assert(hub_sound_start() && !hub_sound_get().enabled);
    c.enabled=true;c.tone=2;c.volume=50;hub_sound_set(c);drain();
    assert(hub_sound_start() && hub_sound_get().tone==2 && hub_sound_get().volume==50);
    write_error=1;hub_sound_preview();drain();assert(hub_sound_error() && cleanups==2);
    write_error=0;save_error=1;c.volume=40;hub_sound_set(c);hub_sound_preview();drain();assert(hub_sound_error());
    int before=starts;
    assert(hub_sound_network_begin());
    hub_sound_preview();drain();assert(starts==before && (queued&PREVIEW));
    hub_sound_network_end();drain();assert(starts==before+1);
    /* An active codec prevents HTTPS acquisition; cleanup releases ownership. */
    atomic_store(&resource_owner,1);assert(!hub_sound_network_begin());
    atomic_store(&resource_owner,0);assert(hub_sound_network_begin());hub_sound_network_end();
    puts("Sound worker persistence, queued mute, audio fault cleanup and save failure: PASS");
}
