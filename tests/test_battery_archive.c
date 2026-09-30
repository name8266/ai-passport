#define _POSIX_C_SOURCE 200809L
#include "battery_archive.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int main(int argc,char **argv) {
    assert(argc==2);assert(battery_archive_open(argv[1]));
    bat_db_t page;care_info_t info;assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));
    bat_asset_t a={.capacity_mah=2000,.soc=18,.health=95};strcpy(a.name,"蓝猫电池");
    for(unsigned i=0;i<140;++i) {
        assert(battery_archive_upsert(&a,0,120,info.revision,BAT_MIN_EPOCH,480)==BAT_OK);
        assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));
    }
    assert(info.total==140 && page.count==BAT_PAGE_SIZE && info.due_count==140);
    unsigned count=0;uint32_t cursor=0;
    do { assert(battery_archive_page(&page,&info,cursor,"",-1,BAT_MIN_EPOCH));count+=page.count;cursor=info.next_cursor; }while(cursor);
    assert(count==140);assert(battery_archive_page(&page,&info,0,"蓝猫",-1,BAT_MIN_EPOCH) && page.count==16);
    assert(battery_archive_previous(32,-1,BAT_MIN_EPOCH)==16);
    care_asset_t timed={.asset={.soc=10,.status=BAT_READY},.updated_at=BAT_MIN_EPOCH,.remind_at=BAT_MIN_EPOCH+86400};
    int64_t at;assert(care_due(&timed,BAT_MIN_EPOCH,&at)==CARE_LOW && at==0);
    timed.asset.soc=80;assert(care_due(&timed,BAT_MIN_EPOCH,&at)==CARE_SCHEDULED && at==timed.remind_at);
    timed.remind_at=0;assert(care_due(&timed,BAT_MIN_EPOCH+8*86400,&at)==CARE_STALE);
    care_pet_t capped={0};care_asset_t samples[6]={0};
    for(unsigned i=0;i<6;++i)(void)care_reward(&capped,&samples[i],BAT_FINISH,true,BAT_MIN_EPOCH,480);
    assert(capped.xp==40 && capped.daily_xp==40);
    assert(care_reward(&capped,&samples[0],BAT_FINISH,true,BAT_MIN_EPOCH+86400,480)==10 && capped.streak==2);
    assert(care_reward(&capped,&samples[0],BAT_FINISH,true,BAT_MIN_EPOCH+86400,480)==0);
    assert(care_reward(&capped,&samples[0],BAT_EDIT,false,BAT_MIN_EPOCH+86400,480)==0);
    assert(care_reward(&capped,&samples[0],BAT_EDIT,true,0,480)==0);
    uint32_t revision=info.revision;
    assert(battery_archive_action(1,BAT_CHARGE,revision-1,BAT_MIN_EPOCH,480)==BAT_CONFLICT);
    assert(battery_archive_action(1,BAT_CHARGE,revision,BAT_MIN_EPOCH,480)==BAT_OK);
    assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH+7201));
    assert(info.due_count==140);
    assert(battery_archive_action(1,BAT_FINISH,info.revision,BAT_MIN_EPOCH+7201,480)==BAT_OK);
    assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH+7201));assert(info.pet.xp==10);
    assert(battery_archive_action(1,BAT_CHARGE,info.revision,BAT_MIN_EPOCH+7202,480)==BAT_OK);
    assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH+7202));
    assert(battery_archive_action(1,BAT_FINISH,info.revision,BAT_MIN_EPOCH+7203,480)==BAT_OK);
    assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH+7203));assert(info.pet.xp==10);
    assert(battery_archive_snooze(BAT_MIN_EPOCH));
    assert(battery_archive_settings(25,22,8,false));
    assert(battery_archive_open(argv[1]));assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));assert(info.pet.xp==10 && info.pet.volume==25);
    for(int stage=1;stage<=4;++stage) {
        battery_archive_fail_stage=stage;
        assert(battery_archive_upsert(&a,0,120,info.revision,BAT_MIN_EPOCH,480)!=BAT_OK);
        battery_archive_fail_stage=0;
        assert(battery_archive_open(argv[1]));assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));
        assert(info.total==(uint32_t)(140+stage));
        uint32_t last=info.revision;
        assert(battery_archive_open(argv[1]));assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));assert(info.revision==last);
    }
    assert(battery_archive_action(140,BAT_RETIRE,info.revision,BAT_MIN_EPOCH,480)==BAT_OK);
    assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));
    assert(battery_archive_action(140,BAT_DELETE,info.revision,BAT_MIN_EPOCH,480)==BAT_OK);
    assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));assert(info.total==143);
    care_asset_t removed;assert(!battery_archive_get(140,&removed));
    bat_db_t old;bat_init(&old);old.count=1;old.assets[0]=a;old.assets[0].id=999;old.next_id=1400;bat_seal(&old);
    assert(battery_archive_migrate(&old));assert(battery_archive_get(999,&removed));
    assert(battery_archive_page(&page,&info,0,"",-1,0) && page.next_id==1400);
    assert(battery_archive_page(&page,&info,0,"",-1,BAT_MIN_EPOCH));
    assert(battery_archive_action(999,BAT_RETIRE,info.revision,0,0)==BAT_OK);
    assert(battery_archive_page(&page,&info,0,"",-1,0));assert(battery_archive_action(999,BAT_DELETE,info.revision,0,0)==BAT_OK);
    assert(battery_archive_open(argv[1]));assert(battery_archive_migrate(&old));assert(!battery_archive_get(999,&removed));
    care_pet_t pet={.quiet_start=22,.quiet_end=8,.volume=20};assert(care_quiet(&pet,BAT_MIN_EPOCH+23*3600,0));assert(!care_quiet(&pet,BAT_MIN_EPOCH+12*3600,0));
    assert(care_stage(&pet)==0);pet.xp=180;assert(care_stage(&pet)==3);
    assert(battery_archive_page(&page,&info,0,"",-1,0));
    bat_event_t audit[16];assert(battery_archive_history(audit,16,0,info.revision)==16);
    assert(battery_archive_history(audit,16,0,info.revision-1)==-1);
    char history_path[256];snprintf(history_path,sizeof(history_path),"%s/history",argv[1]);
    FILE *f=fopen(history_path,"r+b");assert(f);assert(fputc(0x7f,f)!=EOF);fclose(f);
    assert(battery_archive_history(audit,16,0,info.revision)==-1);
    puts("Archive/care: PASS (140 assets, paging, durable replay at 4 stages, migration, reminders, reward caps, quiet hours)");
}
