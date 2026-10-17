#include "pusher_store.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static cp_records_t stored,old_stored;
static bool board_present,legacy_present,setting_present,fail_init,fail_write;
static uint8_t setting;
static unsigned opens,closes,commits;
esp_err_t nvs_flash_init(void){return fail_init?-1:ESP_OK;}
esp_err_t nvs_flash_deinit(void){return ESP_OK;}
const char *esp_err_to_name(esp_err_t err){(void)err;return "injected";}
esp_err_t nvs_open(const char *name,nvs_open_mode_t mode,nvs_handle_t *h){assert(!strcmp(name,"cp_rogue"));(void)mode;*h=1;++opens;return ESP_OK;}
void nvs_close(nvs_handle_t h){assert(h==1);++closes;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *size){
 assert(h==1&&*size==sizeof(stored));
 if(!strcmp(key,"board_v3_180")){if(!board_present)return -1;memcpy(out,&stored,sizeof(stored));return ESP_OK;}
 assert(!strcmp(key,"board_v1"));if(!legacy_present)return -1;
 memcpy(out,&old_stored,sizeof(old_stored));return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *data,size_t size){assert(h==1&&!strcmp(key,"board_v3_180")&&size==sizeof(stored));if(fail_write)return -1;memcpy(&stored,data,size);board_present=true;return ESP_OK;}
esp_err_t nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *out){assert(h==1&&!strcmp(key,"muted"));if(!setting_present)return -1;*out=setting;return ESP_OK;}
esp_err_t nvs_set_u8(nvs_handle_t h,const char *key,uint8_t value){assert(h==1&&!strcmp(key,"muted")&&value<=1);if(fail_write)return -1;setting=value;setting_present=true;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){assert(h==1);++commits;return fail_write?-1:ESP_OK;}
int main(void){
 assert(!cp_store_muted());cp_records_t board,loaded;cp_records_init(&board);board.generation=12;cp_records_seal(&board);
 assert(cp_store_write(&board));cp_records_t before=stored;
 assert(cp_store_set_muted(true)&&cp_store_muted());assert(!memcmp(&before,&stored,sizeof(before)));
 assert(cp_store_load(&loaded)&&loaded.generation==12&&cp_records_valid(&loaded));
 assert(cp_store_set_muted(false)&&!cp_store_muted());assert(!memcmp(&before,&stored,sizeof(before)));
 fail_write=true;assert(!cp_store_set_muted(true)&&!cp_store_muted());fail_write=false;
 setting=99;assert(!cp_store_muted());fail_init=true;assert(!cp_store_muted()&&!cp_store_set_muted(true));fail_init=false;
 /* An old ranking is retained but never mixed with the new ruleset. */
 old_stored=stored;old_stored.discovered=0x00000123u;
 old_stored.rows[0]=(cp_record_t){.sequence=1,.chips=777,.level=2};old_stored.next_sequence=2;
 cp_records_seal(&old_stored);
 cp_records_t untouched=old_stored;legacy_present=true;board_present=false;
 assert(cp_store_load(&loaded)&&cp_records_valid(&loaded));
 assert(loaded.discovered==old_stored.discovered&&!loaded.rows[0].sequence&&loaded.next_sequence==1);
 assert(cp_store_write(&loaded)&&!memcmp(&old_stored,&untouched,sizeof(untouched)));
 assert(cp_store_load(&loaded)&&loaded.discovered==0x123u);
 assert(opens==closes&&commits==4);puts("Mute NVS / separate 180-body leaderboard / inherited discoveries: PASS");
}
