#include "battery_model.h"
#include "battery_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bat_asset_t asset(void) {
    bat_asset_t a={.capacity_mah=2000,.soc=65,.health=96};
    strcpy(a.name,"相机电池"); strcpy(a.location,"A1"); return a;
}
int main(void) {
    bat_db_t db; bat_init(&db); assert(bat_db_valid(&db));
    bat_asset_t a=asset(); assert(bat_upsert(&db,&a,0,0)==BAT_OK);
    assert(db.assets[0].id==1 && db.revision==1 && db.events[0].epoch==0 && bat_db_valid(&db));
    bat_db_t before=db;
    assert(bat_upsert(&db,&a,0,0)==BAT_CONFLICT); assert(!memcmp(&before,&db,sizeof(db)));
    assert(bat_action(&db,1,BAT_FINISH,1,0)==BAT_TRANSITION);
    assert(bat_action(&db,1,BAT_CHECKOUT,1,0)==BAT_OK);
    assert(bat_action(&db,1,BAT_CHARGE,2,0)==BAT_TRANSITION);
    assert(bat_action(&db,1,BAT_RETURN,2,0)==BAT_OK);
    assert(bat_action(&db,1,BAT_CHARGE,3,0)==BAT_OK);
    assert(bat_action(&db,1,BAT_FINISH,4,BAT_MIN_EPOCH)==BAT_OK);
    assert(db.assets[0].soc==100 && db.assets[0].cycles==1);
    assert(bat_action(&db,1,BAT_INSPECT,5,0)==BAT_OK);
    assert(bat_action(&db,1,BAT_RELEASE,6,0)==BAT_OK);
    bat_asset_t edit=db.assets[0]; edit.status=BAT_RETIRED;
    assert(bat_upsert(&db,&edit,7,0)==BAT_TRANSITION);
    edit.status=BAT_READY; edit.soc=10; edit.health=75;
    assert(bat_upsert(&db,&edit,7,0)==BAT_OK);
    bat_summary_t summary; bat_summary(&db,&summary);
    assert(summary.low==1 && summary.attention==1 && summary.capacity_mah==2000);
    assert(bat_action(&db,1,BAT_DELETE,8,0)==BAT_TRANSITION);
    assert(bat_action(&db,1,BAT_RETIRE,8,0)==BAT_OK);
    bat_summary(&db,&summary); assert(summary.attention==0 && summary.capacity_mah==0);
    assert(bat_action(&db,1,BAT_CHECKOUT,9,0)==BAT_TRANSITION);
    assert(bat_action(&db,1,BAT_DELETE,9,0)==BAT_OK && db.count==0);
    assert(bat_db_valid(&db)); assert(bat_action(&db,1,BAT_RETURN,10,0)==BAT_NOT_FOUND);
    for (int i=0;i<BAT_PAGE_SIZE;++i) assert(bat_upsert(&db,&a,db.revision,0)==BAT_OK);
    before=db; assert(bat_upsert(&db,&a,db.revision,0)==BAT_FULL); assert(!memcmp(&before,&db,sizeof(db)));
    assert(db.assets[0].id==2); edit=db.assets[0];
    for (int i=0;i<100;++i) assert(bat_upsert(&db,&edit,db.revision,BAT_MIN_EPOCH+i)==BAT_OK);
    assert(db.event_count==BAT_MAX_EVENTS && db.events[47].sequence==db.event_sequence && bat_db_valid(&db));
    before=db; db.assets[0].soc=101; assert(!bat_db_valid(&db)); bat_seal(&db); assert(!bat_db_valid(&db)); db=before;
    db.assets[1].id=db.assets[0].id; bat_seal(&db); assert(!bat_db_valid(&db)); db=before;
    db.schema=2; bat_seal(&db); assert(!bat_db_valid(&db));
    assert(bat_text_valid("中文 Battery",64,true));
    assert(!bat_text_valid("\xc0\x80",4,false)); assert(!bat_text_valid("\xed\xa0\x80",4,false));
    assert(!bat_text_valid("\xf4\x90\x80\x80",5,false)); assert(!bat_text_valid("\xe4\xb8",3,false));
    assert(!bat_text_valid("abc",3,false)); assert(!bat_text_valid("line\n",6,false));
    bat_clock_t clock={0}; assert(bat_clock_now(&clock,0)==0);
    assert(!bat_clock_sync(&clock,0,480,100)); assert(!bat_clock_sync(&clock,BAT_MIN_EPOCH,841,100));
    assert(bat_clock_sync(&clock,BAT_MIN_EPOCH,480,100));
    assert(bat_clock_now(&clock,5100)==BAT_MIN_EPOCH+5); assert(bat_clock_now(&clock,99)==0);
    assert(bat_clock_sync(&clock,BAT_MAX_EPOCH,0,100)); assert(bat_clock_now(&clock,1100)==0);
    const char *json="{\"name\":\"电池\",\"soc\":10}"; assert(battery_payload_safe(json,strlen(json)));
    json="{\"name\":\"\\u0000evil\"}"; assert(!battery_payload_safe(json,strlen(json)));
    json="{\"name\":\"literal \\\\u0000\"}"; assert(battery_payload_safe(json,strlen(json)));
    assert(!battery_payload_safe("[[[[[0]]]]]",11)); assert(!battery_payload_safe("{\"x\":\"oops}",11));
    const char nul[]={ '{',0,'}' }; assert(!battery_payload_safe(nul,3));
    puts("Battery model/protocol: PASS (transitions, conflicts, bounds, CRC, UTF-8, ring, clock)"); return 0;
}
