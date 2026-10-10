#define _DARWIN_C_SOURCE
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#define HUB_ARCHIVE_PATH "alerts.dat"
#define HUB_ARCHIVE_COMPACT_PATH "alerts.new"
#define HUB_ARCHIVE_COMPACT_TXN_PATH "swap.ok"
/* Exercise the production digest path against FAT's default 8.3 contract. */
static const char *local_name(const char *name) {
    return strncmp(name,"/archive/",9)==0?name+9:name;
}
static FILE *fat_fopen(const char *name,const char *mode) {
    name=local_name(name);const char *dot=strchr(name,'.');
    size_t base=dot?(size_t)(dot-name):strlen(name);
    if(base>8 || (dot && strlen(dot+1)>3)) {errno=EINVAL;return NULL;}
    return fopen(name,mode);
}
static int local_unlink(const char *name) {return unlink(local_name(name));}
#define fopen fat_fopen
#define unlink local_unlink
static unsigned reads;
static size_t counted_read(void *p,size_t size,size_t n,FILE *f) {reads++;return fread(p,size,n,f);}
#define fread counted_read
#include "../firmware/main/hub_archive.c"
static unsigned cursor_updates;
static uint32_t test_cursor;
uint32_t hub_ai_read_cursor(void) {return test_cursor;}
uint32_t hub_ai_current_epoch(void) {return 0; /* offline host test */}
bool hub_ai_set_cursor(uint32_t cursor) {cursor_updates++;test_cursor=cursor;return true;}
bool hub_ai_reset_cursor(void) {return hub_ai_set_cursor(0);}
bool hub_ai_advance_cursor(uint32_t cursor) {if(cursor>test_cursor)test_cursor=cursor;return true;}
static void copy_file(const char *from_path,const char *to_path) {
    FILE *from=fopen(from_path,"rb"),*to=fopen(to_path,"wb");
    assert(from && to);
    unsigned char buffer[512];size_t n;
    while((n=fread(buffer,1,sizeof(buffer),from))>0) assert(fwrite(buffer,1,n,to)==n);
    assert(!ferror(from));assert(fclose(from)==0);assert(fclose(to)==0);
}
int main(void) {
    char dir[]="/tmp/hub-archive-test.XXXXXX";
    assert(mkdtemp(dir));assert(chdir(dir)==0);
    assert(!fat_fopen("/archive/ai-digest.dat","rb") && errno==EINVAL);
    hub_archive_t db;assert(hub_archive_open(&db,1));assert(test_format);
    hub_archive_record_t source={.uid=7,.session=11,.kind=HUB_ARCHIVE_SOURCE};
    assert(hub_archive_capture(&db,&source));
    hub_archive_record_t preview={.uid=7,.session=12,.kind=HUB_ARCHIVE_PREVIEW};
    strcpy(preview.app,"example.app");strcpy(preview.body,"hello");
    assert(hub_archive_capture(&db,&preview));
    /* Same UID from a new BLE connection must not consume an old placeholder. */
    assert(!is_superseded(&db,0));
    preview.session=11;assert(hub_archive_capture(&db,&preview));
    assert(is_superseded(&db,0));
    hub_ai_batch_t batch;assert(hub_archive_collect_since(&db,0,3,&batch));
    assert(batch.count==2 && batch.through_sequence==3);
    fclose(db.file);assert(hub_archive_open(&db,2));assert(is_superseded(&db,0));
    hub_archive_record_t got;assert(hub_archive_get_record(&db,"example.app",0,&got));
    assert(got.sequence==3 && !strcmp(got.body,"hello"));
    /* Combined feed skips superseded placeholders and supports both directions. */
    assert(hub_archive_get_record(&db,"",0,&got) && got.sequence==3);
    assert(hub_archive_get_record(&db,"",1,&got) && got.sequence==2);
    assert(!hub_archive_get_record(&db,"",2,&got));
    assert(hub_archive_get_record(&db,"",0,&got) && got.sequence==3);
    hub_ai_digest_t digest={.processed_through=3,.included=2,
        .day_tag=hub_ai_day_tag(2000000000u)};strcpy(digest.summary,"saved");
    assert(hub_archive_save_digest(&db,&digest));
    assert(hub_archive_digest_revision(&db)==1);
    assert(hub_archive_hide_digest(&db));
    hub_ai_digest_t restored;
    assert(hub_archive_last_digest(&db,&restored) && restored.hidden);
    assert(hub_archive_ack_digest(&db));
    assert(!hub_archive_last_digest(&db,&restored));
    assert(hub_archive_digest_revision(&db)==4);
    /* Corrupt newest tombstone; fallback MUST still be cleared, never old. */
    FILE *f=fopen(ACTIVE_SLOT0,"ab");assert(f);fputc(1,f);fclose(f);
    assert(!hub_archive_last_digest(&db,&restored));
    /* A freshly committed digest has v3 checkpoint and survives reboot. */
    assert(hub_archive_save_digest(&db,&digest));
    assert(hub_archive_digest_revision(&db)==4);
    f=fopen(ACTIVE_SLOT1,"ab");assert(f);fputc(1,f);fclose(f);
    assert(hub_archive_last_digest(&db,&restored));
    assert(!strcmp(restored.summary,"saved"));
    assert(restored.day_tag==hub_ai_day_tag(2000000000u));
    /* Hide/show is reversible and persisted without acknowledging tasks. */
    assert(hub_archive_hide_digest(&db));
    assert(hub_archive_last_digest(&db,&restored) && restored.hidden);
    uint32_t hidden_revision=hub_archive_digest_revision(&db);
    assert(hub_archive_show_digest(&db));
    assert(hub_archive_last_digest(&db,&restored) && !restored.hidden);
    assert(hub_archive_digest_revision(&db)==hidden_revision+1);
    assert(hub_archive_show_digest(&db)); /* idempotent: no needless flash write */
    assert(hub_archive_digest_revision(&db)==hidden_revision+1);

    /* Simulate power loss after digest fsync and before NVS advance. */
    test_cursor=0;
    assert(hub_archive_reconcile_cursor(&db));
    assert(test_cursor==3);
    /* Simulate a pre-Beta18 v2 snapshot on an archive already renumbered.
     * An old checkpoint must NEVER skip new notifications after migration. */
    active_record_t legacy={0};
    legacy.magic=ACTIVE_MAGIC;
    legacy.version=2u;
    legacy.generation=hub_archive_digest_revision(&db)+1u;
    legacy.digest=digest;
    legacy.checksum=active_hash(&legacy);
    FILE *legacy_file=fopen((legacy.generation&1u)?ACTIVE_SLOT1:ACTIVE_SLOT0,"wb");
    assert(legacy_file);
    assert(fwrite(&legacy,sizeof(legacy),1,legacy_file)==1);
    assert(sync_file(legacy_file));assert(fclose(legacy_file)==0);
    test_cursor=0;
    assert(hub_archive_reconcile_cursor(&db) && test_cursor==0);
    /* Once written in v3, a crashed checkpoint can be replayed safely. */
    assert(hub_archive_save_digest(&db,&digest));
    assert(hub_archive_reconcile_cursor(&db) && test_cursor==3);
    test_cursor=0;
    cursor_updates=0;
    fclose(db.file);f=fopen(HUB_ARCHIVE_PATH,"ab");assert(f);fputc(1,f);fclose(f);
    assert(!hub_archive_open(&db,3) && db.failed);fclose(db.file);
    test_blank=false;test_mount_error=true;
    assert(!hub_archive_open(&db,4) && !test_format);
    assert(db.wl==WL_INVALID_HANDLE && !db.volume_mounted && test_wl_unmounts==1);
    test_wl_unmount_error=1;
    assert(!hub_archive_open(&db,5) && db.wl==1 && !db.volume_mounted);
    assert(!hub_archive_initialize(&db,true) && test_erases==0);
    test_wl_unmount_error=0;
    assert(!hub_archive_initialize(&db,false) && test_erases==0);
    test_partition_address=0x500000;
    assert(!hub_archive_initialize(&db,true) && test_erases==0);
    test_partition_address=0x400000;
    db.wl=1;db.volume_mounted=true;test_unmount_error=1;
    assert(!hub_archive_initialize(&db,true) && test_erases==0);
    test_unmount_error=0;test_erase_error=1;
    assert(!hub_archive_initialize(&db,true) && test_erases==0);
    test_erase_error=0;test_mount_error=false;
    assert(hub_archive_initialize(&db,true) && test_erases==1 && db.rows==0);
    assert(!hub_archive_initialize(&db,true) && test_erases==1);
    /* 500 actual full notifications: metadata + detail, then compact batches.
     * Count real stdio calls to guard against O(N^2) checkpoint rescanning. */
    for(uint32_t i=0;i<500;i++) {
        source.uid=i;source.session=99;assert(hub_archive_capture(&db,&source));
        preview.uid=i;preview.session=99;
        memset(preview.body,'a',sizeof(preview.body)-1);preview.body[sizeof(preview.body)-1]=0;
        if(i==250) strcpy(preview.body+130,"OTP");
        assert(hub_archive_capture(&db,&preview));
    }
    reads=0;uint32_t cursor=0,total=0;unsigned calls=0;
    while(cursor<db.rows) {
        assert(hub_archive_collect_since(&db,cursor,8,&batch));
        assert(batch.count && batch.through_sequence>cursor);
        for(unsigned i=0;i<batch.count;i++) {
            assert(strlen(batch.items[i].body)<HUB_AI_BODY_BYTES);
            if(batch.items[i].sequence==502) assert(batch.items[i].sensitive);
        }
        cursor=batch.through_sequence;total+=batch.count;calls++;
    }
    assert(total==500 && calls==63 && reads==1000);
    reads=0;assert(hub_archive_collect_since(&db,UINT32_MAX,8,&batch));
    assert(batch.count==0 && reads==0);
    reads=0;
    for(uint32_t i=0;i<500;i++) {
        assert(hub_archive_get_record(&db,"example.app",i,&got));
        assert(got.sequence==1000-2*i);
        assert(hub_archive_get_record(&db,"example.app",i,&got));
    }
    assert(reads<=2000);
    reads=0;
    for(int i=498;i>=0;i--) {
        assert(hub_archive_get_record(&db,"example.app",(uint32_t)i,&got));
        assert(got.sequence==1000-2*(uint32_t)i);
    }
    assert(reads<=1000);
    /* The combined feed retains linear traversal for a realistic backlog. */
    reads=0;
    for(uint32_t i=0;i<500;i++) {
        assert(hub_archive_get_record(&db,"",i,&got));
        assert(got.sequence==1000-2*i);
    }
    assert(reads<=2000); /* Includes revisiting the cached starting record. */
    reads=0;
    for(int i=498;i>=0;i--) {
        assert(hub_archive_get_record(&db,"",(uint32_t)i,&got));
        assert(got.sequence==1000-2*(uint32_t)i);
    }
    assert(reads<=1000);
    fclose(db.file);assert(hub_archive_open(&db,100));
    assert(db.rows==1000);
    source.uid=999;assert(hub_archive_capture(&db,&source));
    preview.uid=999;assert(hub_archive_capture(&db,&preview));
    assert(hub_archive_get_record(&db,"example.app",0,&got) && got.sequence==1002);
    digest=(hub_ai_digest_t){.processed_through=1002};strcpy(digest.summary,"bounded");
    for(unsigned i=0;i<48;i++)assert(hub_archive_save_digest(&db,&digest));
    assert(hub_archive_last_digest(&db,&digest) && !strcmp(digest.summary,"bounded"));
    assert(hub_archive_digest_revision(&db)>48);
    FILE *state_file=fopen(ACTIVE_SLOT0,"rb");assert(state_file);
    assert(fseek(state_file,0,SEEK_END)==0);
    assert(ftell(state_file)==(long)sizeof(active_record_t));
    assert(fclose(state_file)==0);
    hub_ai_digest_t todo={.processed_through=1002,.task_count=2};
    strcpy(todo.summary,"两个待办");
    strcpy(todo.tasks[0].task,"完成报告");
    strcpy(todo.tasks[0].source,"飞书");
    strcpy(todo.tasks[1].task,"领取快递");
    strcpy(todo.tasks[1].source,"菜鸟");
    assert(hub_archive_save_digest(&db,&todo));
    uint32_t original_revision=hub_archive_digest_revision(&db);
    uint32_t complete_id=hub_ai_task_id(&todo.tasks[0]);
    assert(!hub_archive_complete_task(&db,complete_id,original_revision-1));
    assert(!hub_archive_complete_task(&db,0,original_revision));
    assert(hub_archive_complete_task(&db,complete_id,original_revision));
    assert(hub_archive_digest_revision(&db)==original_revision+2);
    assert(!hub_archive_complete_task(&db,complete_id,original_revision));
    assert(hub_archive_last_digest(&db,&restored));
    assert(restored.task_count==1 && !strcmp(restored.tasks[0].task,"领取快递"));
    assert(restored.summary[0]);
    /* Torn newest snapshot must fall back to the preceding COMPLETED copy,
     * never the earlier version with a revived task. */
    FILE *torn=fopen(((original_revision+2u)&1u)?ACTIVE_SLOT1:ACTIVE_SLOT0,"ab");
    assert(torn);fputc(0xaa,torn);assert(fclose(torn)==0);
    assert(hub_archive_last_digest(&db,&restored));
    assert(restored.task_count==1 && !strcmp(restored.tasks[0].task,"领取快递"));

    fclose(db.file);assert(hub_archive_open(&db,103));
    assert(hub_archive_last_digest(&db,&restored) && restored.task_count==1);
    /* Exact revision guards against deleting an item replaced during AI. */
    assert(!hub_archive_complete_task(&db,complete_id,original_revision));
    puts("Rolling digest two-slot durability, bounded Flash, read/hide and JSON-ready task state: PASS");
    assert(unlink(ACTIVE_SLOT0)==0);
    assert(unlink(ACTIVE_SLOT1)==0);
    fclose(db.file);
    assert(unlink(HUB_ARCHIVE_PATH)==0);
    assert(hub_archive_open(&db,101));
    hub_archive_group_t g;
    /* Actual burst ordering: every source precedes all detailed attributes.
     * Duplicate source updates must neither evict another UID nor leave an
     * extra unresolved placeholder. Reboot must rebuild the same result. */
    source.session=preview.session=101;
    for(uint32_t i=0;i<500;i++) {
        source.uid=2000+i;assert(hub_archive_capture(&db,&source));
    }
    for(uint32_t i=0;i<50;i++) {
        source.uid=2000+i;assert(hub_archive_capture(&db,&source));
    }
    for(uint32_t i=0;i<500;i++) {
        preview.uid=2000+i;assert(hub_archive_capture(&db,&preview));
    }
    assert(db.rows==1050);
    assert(hub_archive_get_group(&db,0,&g) && g.count==500);
    assert(!hub_archive_get_group(&db,1,&g));
    fclose(db.file);assert(hub_archive_open(&db,102));
    assert(hub_archive_get_group(&db,0,&g) && g.count==500);
    assert(!hub_archive_get_group(&db,1,&g));
    /* Deliberately collide fingerprints. Exact UID/session verification must
     * leave the other source pending until its own preview arrives. */
    uint32_t seen_keys[262144]={0},first=0,second=0;
    for(uint32_t uid=10000;uid<20000 && !second;uid++) {
        uint32_t key=pending_key(uid,102);
        if(seen_keys[key]) {first=seen_keys[key];second=uid;}
        else seen_keys[key]=uid;
    }
    assert(second && first!=second);
    source.session=preview.session=102;
    source.uid=first;assert(hub_archive_capture(&db,&source));
    source.uid=second;assert(hub_archive_capture(&db,&source));
    preview.uid=second;assert(hub_archive_capture(&db,&preview));
    assert(db.groups[group_index(&db,"Unresolved",false)].count==1);
    preview.uid=first;assert(hub_archive_capture(&db,&preview));
    assert(!hub_archive_get_group(&db,1,&g));
    fclose(db.file);assert(hub_archive_open(&db,103));
    assert(hub_archive_get_group(&db,0,&g) && g.count==502);
    assert(!hub_archive_get_group(&db,1,&g));
    puts("500-source burst, duplicate metadata, fingerprint collision and reboot: PASS");

    /* Expire only records with trusted timestamps and verify the atomic
     * replacement can recover after power loss between unlink and rename. */
    uint32_t now=2000000000u,rows_before=db.rows,removed=0;
    test_cursor=rows_before+1; /* checkpoint includes the expired row below */
    hub_archive_record_t timed={.uid=9001,.session=103,
        .kind=HUB_ARCHIVE_PREVIEW,.reserved=HUB_ARCHIVE_TIME_EPOCH};
    timed.elapsed_seconds=now-40u*86400u;
    strcpy(timed.app,"ttl.app");strcpy(timed.title,"expired");
    assert(hub_archive_capture(&db,&timed));
    timed.uid++;timed.elapsed_seconds=now-5u*86400u;
    strcpy(timed.title,"recent");assert(hub_archive_capture(&db,&timed));
    digest=(hub_ai_digest_t){.processed_through=db.rows};strcpy(digest.summary,"stale summary");
    assert(hub_archive_save_digest(&db,&digest));
    assert(hub_archive_expire(&db,now,30,&removed));
    assert(removed==1 && db.rows==rows_before+1);
    assert(cursor_updates==1 && test_cursor==rows_before);
    assert(hub_archive_last_digest(&db,&digest) && !strcmp(digest.summary,"stale summary"));
    assert(digest.processed_through==rows_before);
    uint8_t count=0;uint32_t next=0;bool older=false;
    hub_archive_record_t recent[HUB_ARCHIVE_WEB_PAGE_SIZE];
    assert(hub_archive_get_recent(&db,UINT32_MAX,HUB_ARCHIVE_WEB_PAGE_SIZE,
                                  recent,&count,&next,&older));
    assert(count==HUB_ARCHIVE_WEB_PAGE_SIZE && recent[0].sequence==db.rows &&
           !strcmp(recent[0].title,"recent"));
    assert(older);
    assert(hub_archive_get_recent(&db,0,HUB_ARCHIVE_WEB_PAGE_SIZE,
                                  recent,&count,&next,&older));
    assert(count==0 && !older);

    uint32_t retained_rows=db.rows;
    copy_file(HUB_ARCHIVE_PATH,HUB_ARCHIVE_COMPACT_PATH);
    compact_txn_t transaction={.magic=COMPACT_TXN_MAGIC,.rows=retained_rows,
                               .cursor=test_cursor};
    transaction.checksum=compact_txn_checksum(&transaction);
    FILE *marker=fopen(HUB_ARCHIVE_COMPACT_TXN_PATH,"wb");assert(marker);
    assert(fwrite(&transaction,sizeof(transaction),1,marker)==1);
    assert(sync_file(marker));assert(fclose(marker)==0);
    fclose(db.file);db.file=NULL;
    assert(unlink(HUB_ARCHIVE_PATH)==0); /* power cut at the vulnerable step */
    assert(hub_archive_open(&db,104));
    assert(db.rows==retained_rows && cursor_updates==2 && test_cursor==rows_before);
    assert(access(HUB_ARCHIVE_COMPACT_PATH,F_OK)!=0 && access(HUB_ARCHIVE_COMPACT_TXN_PATH,F_OK)!=0);
    assert(hub_archive_get_recent(&db,UINT32_MAX,HUB_ARCHIVE_WEB_PAGE_SIZE,
                                  recent,&count,&next,&older));
    assert(count==HUB_ARCHIVE_WEB_PAGE_SIZE && !strcmp(recent[0].title,"recent"));
    puts("30-day retention preserves unread records and rolling digest; compaction power-loss recovery: PASS");
    assert(hub_archive_prune_processed(&db,&removed));
    assert(removed==rows_before && db.rows==1 && test_cursor==0);
    assert(hub_archive_last_digest(&db,&digest) && !strcmp(digest.summary,"stale summary"));
    assert(digest.processed_through==0);
    assert(hub_archive_reconcile_cursor(&db) && test_cursor==0);
    /* A 40-day-old notification NOT YET submitted to AI must never be
     * discarded by periodic retention merely because its timestamp is old. */
    timed.uid=99999;timed.elapsed_seconds=now-40u*86400u;
    strcpy(timed.title,"未总结的旧通知");
    assert(hub_archive_capture(&db,&timed));
    const uint32_t still_pending=db.rows;
    removed=1234;
    assert(hub_archive_expire(&db,now,30,&removed));
    assert(removed==0 && db.rows==still_pending);
    assert(hub_archive_get_record(&db,"ttl.app",0,&got) &&
           !strcmp(got.title,"未总结的旧通知"));

    fclose(db.file);
    assert(unlink(HUB_ARCHIVE_PATH)==0);
    assert(unlink_if_present(ACTIVE_SLOT0));
    assert(unlink_if_present(ACTIVE_SLOT1));
    assert(unlink_if_present(DIGEST_PATH));
    assert(chdir("/")==0);assert(rmdir(dir)==0);
    puts("Archive correlation, reboot reindex, torn writes, nonblank protection and explicit recovery failures: PASS");
}
