#include "hub_archive.h"
#include "hub_ai_filter.h"
#include "hub_ai_state.h"
#include "hub_app_catalog.h"
#include <stddef.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"

static const char *TAG="hub_archive";
_Static_assert(sizeof(hub_archive_record_t)==384, "Archive on-disk layout changed");
#ifndef HUB_ARCHIVE_COMPACT_PATH
#define HUB_ARCHIVE_COMPACT_PATH "/archive/alerts.new"
#endif
#ifndef HUB_ARCHIVE_COMPACT_TXN_PATH
#define HUB_ARCHIVE_COMPACT_TXN_PATH "/archive/swap.ok"
#endif
#ifndef DIGEST_PATH
#define DIGEST_PATH "/archive/digest.dat"
#endif

typedef struct {uint32_t magic,rows,cursor,checksum;} compact_txn_t;
#define COMPACT_TXN_MAGIC 0x4854584eu
/* Dual-slot checkpoint must move with journal sequence renumbering. */
static bool align_active_digest_checkpoint(uint32_t cursor);

/* Noncryptographic corruption check, NOT an anti-tamper signature. */
static uint32_t checksum(const hub_archive_record_t *r) {
    const uint8_t *p=(const uint8_t *)r;
    uint32_t h=2166136261u;
    for (size_t i=0;i<offsetof(hub_archive_record_t,checksum);i++)
        h=(h ^ p[i])*16777619u;
    return h;
}
static bool valid(const hub_archive_record_t *r) {
    return r->magic==HUB_ARCHIVE_MAGIC &&
        r->version==HUB_ARCHIVE_VERSION &&
        r->bytes==sizeof(*r) &&
        r->checksum==checksum(r) &&
        memchr(r->app,0,sizeof(r->app)) &&
        memchr(r->title,0,sizeof(r->title)) &&
        memchr(r->body,0,sizeof(r->body)) &&
        (r->reserved==0 ||
         (r->reserved==HUB_ARCHIVE_TIME_EPOCH && r->elapsed_seconds>=1700000000u));
}
static int group_index(hub_archive_t *db,const char *app,bool create) {
    if(!app || !*app) app="Unresolved";
    for(uint16_t i=0;i<db->group_count;i++)
        if(strcmp(db->groups[i].app,app)==0) return (int)i;
    if(!create) return -1;
    /* A resolved placeholder may have left an empty group slot. Reuse it. */
    uint16_t idx=db->group_count;
    for(uint16_t i=0;i<db->group_count;i++)
        if(db->groups[i].count==0) {idx=i;break;}
    if(idx==db->group_count) {
        if(db->group_count>=HUB_ARCHIVE_GROUP_LIMIT) return -1;
        db->group_count++;
    }
    snprintf(db->groups[idx].app,sizeof(db->groups[idx].app),"%s",app);
    db->groups[idx].count=0;
    return idx;
}
static bool partition_is_blank(const esp_partition_t *p) {
    /* Inspect the entire partition. A damaged empty first sector is NOT
     * evidence that the rest of a user's archive may be reformatted. */
    /* A small bounded scan buffer keeps this startup-only operation from
     * reserving an extra 4 KiB on the worker stack for its whole lifetime. */
    uint8_t buffer[512];
    if(!p || (p->size % sizeof(buffer))!=0) return false;
    for(size_t offset=0;offset<p->size;offset+=sizeof(buffer)) {
        if(esp_partition_read(p,offset,buffer,sizeof(buffer))!=ESP_OK) return false;
        for(size_t i=0;i<sizeof(buffer);i++)
            if(buffer[i]!=0xFF) return false;
    }
    return true;
}
static bool load_entry(hub_archive_t *db,uint32_t slot,hub_archive_record_t *out) {
    if(!db->file || slot>=db->rows) return false;
    if(fseek(db->file,(long)slot*sizeof(*out),SEEK_SET)!=0) return false;
    if(fread(out,sizeof(*out),1,db->file)!=1) {
        clearerr(db->file);
        return false;
    }
    return valid(out);
}
static bool sync_file(FILE *f) {
    if(fflush(f)!=0) return false;
    return fsync(fileno(f))==0;
}
static bool record_pending(hub_archive_t *db,uint32_t uid,uint32_t session,uint32_t offset);
static bool fold_preview(hub_archive_t *db,const hub_archive_record_t *preview);
static bool is_superseded(const hub_archive_t *db,uint32_t index);

static uint32_t compact_txn_checksum(const compact_txn_t *txn) {
    uint32_t h=2166136261u;
    const uint8_t *p=(const uint8_t *)txn;
    for(size_t i=0;i<offsetof(compact_txn_t,checksum);i++)
        h=(h^p[i])*16777619u;
    return h;
}
static bool archive_file_valid(const char *path,uint32_t expected_rows,
                               bool check_expected) {
    FILE *file=fopen(path,"rb");
    if(!file) return false;
    bool ok=fseek(file,0,SEEK_END)==0;
    long size=ok?ftell(file):-1;
    if(size<0 || size%(long)sizeof(hub_archive_record_t)!=0) ok=false;
    uint32_t rows=ok?(uint32_t)(size/(long)sizeof(hub_archive_record_t)):0;
    if(rows>HUB_ARCHIVE_MAX_RECORDS || (check_expected && rows!=expected_rows)) ok=false;
    if(ok && fseek(file,0,SEEK_SET)!=0) ok=false;
    hub_archive_record_t row;
    for(uint32_t i=0;ok && i<rows;i++) {
        ok=fread(&row,sizeof(row),1,file)==1 && valid(&row) &&
           row.sequence==i+1 &&
           (row.kind==HUB_ARCHIVE_SOURCE || row.kind==HUB_ARCHIVE_PREVIEW);
    }
    if(fclose(file)!=0) ok=false;
    return ok;
}
static bool unlink_if_present(const char *path) {
    if(unlink(path)==0 || errno==ENOENT) return true;
    return false;
}
/* A ready marker makes replacement recoverable if power is lost between
 * removing the old path and installing the fully synced compacted journal. */
static bool recover_compaction(void) {
    FILE *marker=fopen(HUB_ARCHIVE_COMPACT_TXN_PATH,"rb");
    if(!marker) {
        if(errno!=ENOENT) return false;
        return unlink_if_present(HUB_ARCHIVE_COMPACT_PATH);
    }
    compact_txn_t txn={0};
    bool marker_ok=fread(&txn,sizeof(txn),1,marker)==1 &&
        txn.magic==COMPACT_TXN_MAGIC && txn.checksum==compact_txn_checksum(&txn);
    if(fclose(marker)!=0) marker_ok=false;
    bool main_ok=archive_file_valid(HUB_ARCHIVE_PATH,0,false);
    if(!marker_ok) {
        /* A damaged marker cannot justify replacing the current journal. */
        if(!main_ok) return false;
        return unlink_if_present(HUB_ARCHIVE_COMPACT_PATH) &&
               unlink_if_present(HUB_ARCHIVE_COMPACT_TXN_PATH);
    }
    bool temp_ok=archive_file_valid(HUB_ARCHIVE_COMPACT_PATH,txn.rows,true);
    bool main_new_ok=archive_file_valid(HUB_ARCHIVE_PATH,txn.rows,true);
    if(temp_ok) {
        if(!unlink_if_present(HUB_ARCHIVE_PATH) ||
           rename(HUB_ARCHIVE_COMPACT_PATH,HUB_ARCHIVE_PATH)!=0) return false;
    }else if(!main_new_ok) {
        if(!main_ok) return false;
        /* The old journal is intact; abandon an incomplete temp transaction. */
        return unlink_if_present(HUB_ARCHIVE_COMPACT_PATH) &&
               unlink_if_present(HUB_ARCHIVE_COMPACT_TXN_PATH);
    }
    if(!unlink_if_present(DIGEST_PATH) || !hub_ai_set_cursor(txn.cursor) ||
       !align_active_digest_checkpoint(txn.cursor)) return false;
    return unlink_if_present(HUB_ARCHIVE_COMPACT_TXN_PATH);
}

static bool rebuild_index(hub_archive_t *db) {
    db->failed=false;db->full=false;db->rows=0;db->next_sequence=1;
    db->group_count=0;db->pending_cursor=0;db->browse_valid=false;
    memset(db->groups,0,sizeof(db->groups));
    memset(db->pending,0,sizeof(db->pending));
    memset(db->superseded,0,sizeof(db->superseded));
    if(fseek(db->file,0,SEEK_END)!=0) {db->failed=true;return false;}
    long size=ftell(db->file);
    if(size<0) {db->failed=true;return false;}
    if((size%(long)sizeof(hub_archive_record_t))!=0) {
        db->failed=true;ESP_LOGE(TAG,"Partial archive tail; refusing writes");return false;
    }
    db->rows=(uint32_t)size/sizeof(hub_archive_record_t);
    if(db->rows>HUB_ARCHIVE_MAX_RECORDS) {db->failed=true;return false;}
    if(db->rows==HUB_ARCHIVE_MAX_RECORDS) db->full=true;
    if((uint32_t)size+sizeof(hub_archive_record_t)>db->usable_bytes) db->full=true;
    if(fseek(db->file,0,SEEK_SET)!=0) {db->failed=true;return false;}
    hub_archive_record_t row;
    for(uint32_t i=0;i<db->rows;i++) {
        if(fread(&row,sizeof(row),1,db->file)!=1 || !valid(&row)) {
            db->failed=true;ESP_LOGE(TAG,"Corrupt archive record at slot %lu",(unsigned long)i);return false;
        }
        if(row.sequence!=db->next_sequence || row.sequence==UINT32_MAX ||
           (row.kind!=HUB_ARCHIVE_SOURCE && row.kind!=HUB_ARCHIVE_PREVIEW)) {
            db->failed=true;return false;
        }
        db->next_sequence=row.sequence+1;
        if(row.obsolete) {db->failed=true;return false;}
        int idx=group_index(db,row.app,true);
        if(idx<0) {db->failed=true;ESP_LOGE(TAG,"Group index exhausted; archive remains untouched");return false;}
        db->groups[idx].count++;
        if(row.kind==HUB_ARCHIVE_SOURCE) {
            if(!record_pending(db,row.uid,row.session,i)) return false;
        }else if(!fold_preview(db,&row)) return false;
    }
    db->mounted=true;
    ESP_LOGI(TAG,"Archive mounted: %lu records, %u application groups",
             (unsigned long)db->rows,(unsigned)db->group_count);
    return true;
}

bool hub_archive_open(hub_archive_t *db,uint32_t session) {
    if(!db) return false;
    memset(db,0,sizeof(*db));
    db->wl=WL_INVALID_HANDLE;
    db->session=session;
    db->next_sequence=1;
    const esp_partition_t *p=esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_FAT,"archive");
    if(!p) {db->failed=true;ESP_LOGE(TAG,"Archive partition not found");return false;}
    /* Nonblank invalid volumes are never silently formatted. */
    bool blank=partition_is_blank(p);
    const esp_vfs_fat_mount_config_t cfg={
        .format_if_mount_failed=blank,
        .max_files=4,
        .allocation_unit_size=4096,
    };
    esp_err_t err=esp_vfs_fat_spiflash_mount_rw_wl(
        "/archive","archive",&cfg,&db->wl);
    if(err!=ESP_OK) {
        /* IDF 5.5.3 can mount WL then fail FAT without releasing WL or
         * registering a FAT context. A FAT unmount cannot clean that handle. */
        if(db->wl!=WL_INVALID_HANDLE && wl_unmount(db->wl)==ESP_OK)
            db->wl=WL_INVALID_HANDLE;
        db->failed=true;
        ESP_LOGE(TAG,"Archive mount failed; existing data preserved (%s)",
                 esp_err_to_name(err));
        return false;
    }
    db->volume_mounted=true;
    if(!recover_compaction()) {
        db->failed=true;
        ESP_LOGE(TAG,"Archive cleanup transaction recovery failed; data preserved");
        return false;
    }
    db->file=fopen(HUB_ARCHIVE_PATH,"rb+");
    if(!db->file && errno==ENOENT) db->file=fopen(HUB_ARCHIVE_PATH,"wb+");
    if(!db->file) {db->failed=true;return false;}
    /* Limit so a full journal does not hit filesystem metadata exhaustion. */
    db->usable_bytes=p->size-32768-HUB_ARCHIVE_DIGEST_BYTES;
    return rebuild_index(db);
}
static bool is_superseded(const hub_archive_t *db,uint32_t index) {
    return index<HUB_ARCHIVE_MAX_RECORDS &&
        (db->superseded[index/8] & (uint8_t)(1u<<(index%8)))!=0;
}
/* Every filesystem operation is serialized by the archive worker. Keep the
 * verification scratch off its small stack. Fingerprints are never identities:
 * a matching entry is always read and checked using full UID and BLE session. */
static hub_archive_record_t pending_scratch;
_Static_assert(HUB_ARCHIVE_MAX_RECORDS<16384,"Pending slot encoding too small");
static uint32_t pending_key(uint32_t uid,uint32_t session) {
    uint32_t h=uid^(session*0x9e3779b9u);
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return h&0x3ffffu;
}
static int find_pending(hub_archive_t *db,uint32_t uid,uint32_t session) {
    uint32_t key=pending_key(uid,session);
    long position=ftell(db->file);
    if(position<0) {db->failed=true;return -2;}
    int found=-1;
    for(size_t i=0;i<sizeof(db->pending)/sizeof(db->pending[0]);i++) {
        uint32_t value=db->pending[i];
        if(!value || (value>>14)!=key) continue;
        uint32_t slot=(value&0x3fffu)-1u;
        if(!load_entry(db,slot,&pending_scratch)) {found=-2;break;}
        if(pending_scratch.kind==HUB_ARCHIVE_SOURCE &&
           pending_scratch.uid==uid && pending_scratch.session==session) {
            found=(int)i;break;
        }
    }
    /* Reindexing is sequential; candidate verification must not move its cursor. */
    if(fseek(db->file,position,SEEK_SET)!=0) found=-2;
    if(found==-2) db->failed=true;
    return found;
}
static void supersede_source(hub_archive_t *db,uint32_t slot) {
    if(slot>=db->rows || is_superseded(db,slot)) return;
    db->superseded[slot/8]|=(uint8_t)(1u<<(slot%8));
    int group=group_index(db,"Unresolved",false);
    if(group>=0 && db->groups[group].count) db->groups[group].count--;
}
static bool record_pending(hub_archive_t *db,uint32_t uid,uint32_t session,
                           uint32_t offset) {
    int existing=find_pending(db,uid,session);
    if(existing==-2) return false;
    size_t index;
    if(existing>=0) {
        index=(size_t)existing;
        supersede_source(db,(db->pending[index]&0x3fffu)-1u);
    }else {
        index=db->pending_cursor;
        db->pending_cursor=(uint16_t)((index+1u)%
                            (sizeof(db->pending)/sizeof(db->pending[0])));
    }
    db->pending[index]=(pending_key(uid,session)<<14)|(offset+1u);
    return true;
}
static bool fold_preview(hub_archive_t *db,
                         const hub_archive_record_t *preview) {
    int index=find_pending(db,preview->uid,preview->session);
    if(index==-2) return false;
    if(index>=0) {
        supersede_source(db,(db->pending[index]&0x3fffu)-1u);
        db->pending[index]=0;
    }
    return true;
}
bool hub_archive_capture(hub_archive_t *db,const hub_archive_record_t *input) {
    if(!db || !db->mounted || db->full || db->failed || !input) return false;
    if(input->kind!=HUB_ARCHIVE_SOURCE && input->kind!=HUB_ARCHIVE_PREVIEW)
        return false;
    if(db->rows>=HUB_ARCHIVE_MAX_RECORDS ||
       (db->rows+1u)*sizeof(hub_archive_record_t)>db->usable_bytes) {
        db->full=true;
        return false;
    }
    /* Refuse new distinct groups when the in-RAM group index is at capacity
     * rather than invisibly archiving entries that cannot be browsed. */
    if(group_index(db,input->app[0]?input->app:"Unresolved",false)<0 &&
       db->group_count>=HUB_ARCHIVE_GROUP_LIMIT) {
        bool empty=false;
        for(uint16_t i=0;i<db->group_count;i++)
            if(db->groups[i].count==0) empty=true;
        if(!empty) { db->full=true; return false; }
    }
    hub_archive_record_t r=*input;
    r.magic=HUB_ARCHIVE_MAGIC;
    r.version=HUB_ARCHIVE_VERSION;
    r.bytes=sizeof(r);
    r.session=input->session; /* ANCS UIDs are scoped to a BLE connection. */
    r.sequence=db->next_sequence++;
    if(input->reserved==HUB_ARCHIVE_TIME_EPOCH && input->elapsed_seconds>=1700000000u) {
        r.elapsed_seconds=input->elapsed_seconds;
        r.reserved=HUB_ARCHIVE_TIME_EPOCH;
    }else {
        r.elapsed_seconds=(uint32_t)(esp_timer_get_time()/1000000);
        r.reserved=0;
    }
    r.obsolete=0;
    r.app[sizeof(r.app)-1]=0;
    r.title[sizeof(r.title)-1]=0;
    r.body[sizeof(r.body)-1]=0;
    if(!r.app[0]) snprintf(r.app,sizeof(r.app),"%s","Unresolved");
    r.checksum=checksum(&r);
    if(fseek(db->file,0,SEEK_END)!=0 ||
       fwrite(&r,sizeof(r),1,db->file)!=1 ||
       !sync_file(db->file)) {
        db->failed=true;ESP_LOGE(TAG,"Archive write failed");return false;
    }
    uint32_t new_slot=db->rows++;
    int group=group_index(db,r.app,true);
    if(group<0) {
        db->failed=true;
        ESP_LOGE(TAG,"Archive written but group index capacity exceeded");
        return false;
    }
    db->groups[group].count++;
    if(r.kind==HUB_ARCHIVE_SOURCE)
        return record_pending(db,r.uid,r.session,new_slot);
    return fold_preview(db,&r);
}
bool hub_archive_get_group(const hub_archive_t *db,uint32_t ordinal,
                           hub_archive_group_t *out) {
    if(!db || !db->mounted || !out) return false;
    /* Skip groups that became empty when placeholders were superseded. */
    for(uint16_t i=0;i<db->group_count;i++) {
        if(!db->groups[i].count) continue;
        if(ordinal==0) {*out=db->groups[i];return true;}
        ordinal--;
    }
    return false;
}
bool hub_archive_get_record(hub_archive_t *db,const char *app,
                            uint32_t nth,hub_archive_record_t *out) {
    if(!db || !db->mounted || !app || !out) return false;
    if(db->browse_valid && db->browse_rows==db->rows &&
       strcmp(db->browse_app,app)==0 && nth<db->browse_ordinal) {
        uint32_t ordinal=db->browse_ordinal;
        for(uint32_t slot=db->browse_slot+1;slot<db->rows;slot++) {
            hub_archive_record_t r;
            if(!load_entry(db,slot,&r)) {db->failed=true;return false;}
            if(!is_superseded(db,slot) && (!app[0] || strcmp(r.app,app)==0) && --ordinal==nth) {
                *out=r;db->browse_slot=slot;db->browse_ordinal=nth;return true;
            }
        }
        return false;
    }
    uint32_t start=db->rows, ordinal=0;
    if(db->browse_valid && db->browse_rows==db->rows &&
       strcmp(db->browse_app,app)==0 && nth>=db->browse_ordinal) {
        start=db->browse_slot+1;ordinal=db->browse_ordinal;
    }
    for(uint32_t i=start;i>0;i--) {
        hub_archive_record_t r;
        if(!load_entry(db,i-1,&r)) {db->failed=true;return false;}
        if(!is_superseded(db,i-1) && (!app[0] || strcmp(r.app,app)==0)) {
            if(ordinal++==nth) {
                *out=r;db->browse_valid=true;db->browse_rows=db->rows;
                db->browse_slot=i-1;db->browse_ordinal=nth;
                snprintf(db->browse_app,sizeof(db->browse_app),"%s",app);
                return true;
            }
        }
    }
    return false;
}

bool hub_archive_get_recent(hub_archive_t *db,uint32_t before_slot,uint8_t limit,
                            hub_archive_record_t *out,uint8_t *count,
                            uint32_t *next_cursor,bool *has_older) {
    if(!db || !db->mounted || db->failed || !out || !count || !next_cursor ||
       !has_older || !limit || limit>HUB_ARCHIVE_WEB_PAGE_SIZE) return false;
    uint32_t slot=(before_slot==UINT32_MAX || before_slot>db->rows)?db->rows:before_slot;
    *count=0;
    while(slot>0 && *count<limit) {
        uint32_t index=--slot;
        if(is_superseded(db,index)) continue;
        if(!load_entry(db,index,&out[*count])) {db->failed=true;return false;}
        (*count)++;
    }
    *next_cursor=slot;
    *has_older=false;
    for(uint32_t i=0;i<slot;i++) {
        if(!is_superseded(db,i)) {*has_older=true;break;}
    }
    return true;
}

static bool record_expired(const hub_archive_record_t *row,uint32_t now,
                           uint32_t age_seconds) {
    return row->reserved==HUB_ARCHIVE_TIME_EPOCH &&
           row->elapsed_seconds<=now && now-row->elapsed_seconds>=age_seconds;
}

static bool compact_archive(hub_archive_t *db,uint32_t now_epoch,
                         uint16_t retention_days,uint32_t *removed_rows,
                         bool prune_committed) {
    if(removed_rows) *removed_rows=0;
    if(!db || !db->mounted || db->failed || !db->file ||
       (!prune_committed && now_epoch<1700000000u) ||
       retention_days<1 || retention_days>365)
        return false;
    uint32_t age_seconds=(uint32_t)retention_days*86400u;
    uint32_t expired=0,retained_cursor=0;
    uint32_t old_cursor=hub_ai_read_cursor();
    if(old_cursor>db->rows) return false; /* corrupt NVS checkpoint */
    hub_archive_record_t row;
    if(fseek(db->file,0,SEEK_SET)!=0) {db->failed=true;return false;}
    for(uint32_t i=0;i<db->rows;i++) {
        if(fread(&row,sizeof(row),1,db->file)!=1 || !valid(&row) || row.sequence!=i+1) {
            db->failed=true;return false;
        }
        if(row.sequence<=old_cursor &&
           (prune_committed || record_expired(&row,now_epoch,age_seconds))) expired++;
        else if(row.sequence<=old_cursor) retained_cursor++;
    }
    if(!expired) return true;

    FILE *compacted=fopen(HUB_ARCHIVE_COMPACT_PATH,"wb");
    if(!compacted) return false;
    uint32_t kept=0;
    bool ok=true;
    if(fseek(db->file,0,SEEK_SET)!=0) ok=false;
    for(uint32_t i=0;i<db->rows && ok;i++) {
        if(fread(&row,sizeof(row),1,db->file)!=1 || !valid(&row) || row.sequence!=i+1)
            {ok=false;break;}
        if(row.sequence<=old_cursor &&
           (prune_committed || record_expired(&row,now_epoch,age_seconds))) continue;
        row.sequence=++kept;
        row.checksum=checksum(&row);
        ok=fwrite(&row,sizeof(row),1,compacted)==1;
    }
    if(ok) ok=sync_file(compacted);
    if(fclose(compacted)!=0) ok=false;
    if(!ok) {
        (void)unlink_if_present(HUB_ARCHIVE_COMPACT_PATH);
        return false;
    }

    compact_txn_t txn={.magic=COMPACT_TXN_MAGIC,.rows=kept,.cursor=retained_cursor};
    txn.checksum=compact_txn_checksum(&txn);
    FILE *marker=fopen(HUB_ARCHIVE_COMPACT_TXN_PATH,"wb");
    if(!marker) {
        (void)unlink_if_present(HUB_ARCHIVE_COMPACT_PATH);
        return false;
    }
    ok=fwrite(&txn,sizeof(txn),1,marker)==1 && sync_file(marker);
    if(fclose(marker)!=0) ok=false;
    if(!ok) {
        (void)unlink_if_present(HUB_ARCHIVE_COMPACT_TXN_PATH);
        (void)unlink_if_present(HUB_ARCHIVE_COMPACT_PATH);
        return false;
    }

    if(fclose(db->file)!=0) {
        db->file=NULL;db->failed=true;return false;
    }
    db->file=NULL;
    if(!unlink_if_present(HUB_ARCHIVE_PATH) ||
       rename(HUB_ARCHIVE_COMPACT_PATH,HUB_ARCHIVE_PATH)!=0) {
        db->failed=true;return false;
    }
    db->file=fopen(HUB_ARCHIVE_PATH,"rb+");
    if(!db->file || !rebuild_index(db)) {db->failed=true;return false;}
    if(!unlink_if_present(DIGEST_PATH) || !hub_ai_set_cursor(retained_cursor) ||
       !align_active_digest_checkpoint(retained_cursor) ||
       !unlink_if_present(HUB_ARCHIVE_COMPACT_TXN_PATH)) {
        db->failed=true;return false;
    }
    if(removed_rows) *removed_rows=expired;
    ESP_LOGI(TAG,"Retention removed %lu expired snapshots; %lu remain",
             (unsigned long)expired,(unsigned long)kept);
    return true;
}

bool hub_archive_expire(hub_archive_t *db,uint32_t now_epoch,
                        uint16_t retention_days,uint32_t *removed_rows) {
    return compact_archive(db,now_epoch,retention_days,removed_rows,false);
}
bool hub_archive_prune_processed(hub_archive_t *db,uint32_t *removed_rows) {
    return compact_archive(db,1700000000u,365,removed_rows,true);
}
#define DIGEST_MAGIC 0x534D4D59u
typedef struct {
    uint32_t magic;
    uint32_t processed_through;
    uint16_t included;
    uint16_t version;
    char text[HUB_AI_SUMMARY_BYTES];
    uint32_t checksum;
} stored_digest_t;

static uint32_t digest_hash(const stored_digest_t *d) {
    const uint8_t *p=(const uint8_t *)d;
    uint32_t h=2166136261u;
    for(size_t i=0;i<offsetof(stored_digest_t,checksum);i++)
        h=(h ^ p[i])*16777619u;
    return h;
}
bool hub_archive_collect_until(hub_archive_t *db,uint32_t cursor,
                               uint32_t last_sequence,uint8_t max_count,
                               hub_ai_batch_t *out) {
    if(!db || !db->mounted || db->failed || !out || max_count==0 ||
       max_count>HUB_AI_MAX_BATCH) return false;
    memset(out,0,sizeof(*out));
    out->after_sequence=cursor;
    /* Open verifies sequence == slot+1. Start at the checkpoint in O(1),
     * then read sequentially; never rescan already processed history. */
    uint32_t last=(last_sequence && last_sequence<db->rows)?
                   last_sequence:db->rows;
    if(cursor>=last)return true;
    if(fseek(db->file,(long)cursor*sizeof(hub_archive_record_t),SEEK_SET)!=0)
        {db->failed=true;return false;}
    for(uint32_t slot=cursor;slot<last && out->count<max_count;slot++) {
        hub_archive_record_t r;
        if(fread(&r,sizeof(r),1,db->file)!=1 || !valid(&r) || r.sequence!=slot+1)
            {db->failed=true;return false;}
        if(r.kind!=HUB_ARCHIVE_PREVIEW || is_superseded(db,slot)) {
            out->through_sequence=r.sequence;
            continue;
        }
        uint16_t day=hub_ai_day_tag(r.reserved==HUB_ARCHIVE_TIME_EPOCH?
                                     r.elapsed_seconds:hub_ai_current_epoch());
        /* Rolling state remains active across midnight. */
        if(day) out->day_tag=day;
        out->through_sequence=r.sequence;
        uint8_t i=out->count++;
        out->items[i].sequence=r.sequence;
        memcpy(out->items[i].app,r.app,sizeof(r.app));
        hub_utf8_copy(out->items[i].title,sizeof(out->items[i].title),
                      (const uint8_t *)r.title,strlen(r.title));
        hub_utf8_copy(out->items[i].body,sizeof(out->items[i].body),
                      (const uint8_t *)r.body,strlen(r.body));
        out->items[i].sensitive=hub_ai_sensitive(r.title,r.body);
    }
    return true;
}
bool hub_archive_collect_since(hub_archive_t *db,uint32_t cursor,
                               uint8_t max_count,hub_ai_batch_t *out) {
    return hub_archive_collect_until(db,cursor,0u,max_count,out);
}

/* Dual-slot generation journal: fsync the next slot before replacing the
 * previous good slot. FAT 8.3 filenames are intentional. */
#define ACTIVE_MAGIC 0x32474844u
#define ACTIVE_VERSION 3u /* same packed layout as Beta17 v2 */
#define ACTIVE_SLOT0 "/archive/sum0.dat"
#define ACTIVE_SLOT1 "/archive/sum1.dat"
/* The archive task exclusively owns this single 1.8 KiB scratch area. */
static hub_ai_digest_t s_archive_digest_scratch;
typedef struct {
    uint32_t magic,version,generation;
    hub_ai_digest_t digest;
    uint32_t checksum;
} active_record_t;
static uint32_t active_hash(const active_record_t *r) {
    const uint8_t *p=(const uint8_t *)r;
    uint32_t h=2166136261u;
    for(size_t i=0;i<offsetof(active_record_t,checksum);i++)
        h=(h^p[i])*16777619u;
    return h;
}
static bool read_slot(const char *path,active_record_t *r) {
    FILE *f=fopen(path,"rb");
    if(!f)return false;
    bool ok=fread(r,sizeof(*r),1,f)==1;
    if(ok && fgetc(f)!=EOF)ok=false;
    if(fclose(f)!=0)ok=false;
    if(!ok || r->magic!=ACTIVE_MAGIC || (r->version!=2u && r->version!=ACTIVE_VERSION) ||
       !r->generation || r->checksum!=active_hash(r) ||
       r->digest.task_count>HUB_AI_TASK_LIMIT ||
       !memchr(r->digest.summary,0,sizeof(r->digest.summary)))return false;
    for(uint8_t i=0;i<r->digest.task_count;i++)
        if(!memchr(r->digest.tasks[i].task,0,HUB_AI_TASK_BYTES) ||
           !memchr(r->digest.tasks[i].source,0,HUB_AI_SOURCE_BYTES) ||
           !memchr(r->digest.tasks[i].due,0,HUB_AI_DUE_BYTES))return false;
    return true;
}
static bool latest_active(hub_ai_digest_t *out,uint32_t *generation,uint32_t *version) {
    static active_record_t slot0,slot1;
    bool a=read_slot(ACTIVE_SLOT0,&slot0),b=read_slot(ACTIVE_SLOT1,&slot1);
    if(!a && !b)return false;
    const active_record_t *best=a && (!b || slot0.generation>slot1.generation)?
        &slot0:&slot1;
    if(out)*out=best->digest;
    if(generation)*generation=best->generation;
    if(version)*version=best->version;
    return true;
}
static bool write_active(const hub_ai_digest_t *d) {
    if(!d || d->task_count>HUB_AI_TASK_LIMIT ||
       (!d->cleared && !d->summary[0]))return false;
    uint32_t gen=0;
    (void)latest_active(NULL,&gen,NULL);
    if(gen==UINT32_MAX)return false;
    static active_record_t record;
    memset(&record,0,sizeof(record));
    record.magic=ACTIVE_MAGIC;
    record.version=ACTIVE_VERSION;
    record.generation=gen+1u;
    record.digest=*d;
    record.checksum=active_hash(&record);
    FILE *f=fopen((record.generation&1u)?ACTIVE_SLOT1:ACTIVE_SLOT0,"wb");
    if(!f)return false;
    bool ok=fwrite(&record,sizeof(record),1,f)==1 && sync_file(f);
    if(fclose(f)!=0)ok=false;
    if(ok)(void)unlink_if_present(DIGEST_PATH);
    return ok;
}
/* Rebase the checkpoint only after the compacted journal is installed.
 * The transaction marker remains until this extra snapshot is durable;
 * a power failure can then safely replay reconciliation. */
static bool align_active_digest_checkpoint(uint32_t cursor) {
    hub_ai_digest_t *state=&s_archive_digest_scratch;
    if(!latest_active(state,NULL,NULL))return true;
    if(state->processed_through==cursor)return true;
    state->processed_through=cursor;
    return write_active(state);
}
bool hub_archive_save_digest(hub_archive_t *db,const hub_ai_digest_t *summary) {
    return db && db->mounted && !db->failed && write_active(summary);
}
static bool hub_archive_legacy_digest(hub_archive_t *db,hub_ai_digest_t *out) {
    if(!db || !db->mounted || !out) return false;
    FILE *file=fopen(DIGEST_PATH,"rb");
    if(!file) return false;
    if(fseek(file,0,SEEK_END)!=0) {fclose(file);return false;}
    long length=ftell(file);
    if(length>=0) length-=length%(long)sizeof(stored_digest_t);
    if(length<(long)sizeof(stored_digest_t) ||
       fseek(file,length-(long)sizeof(stored_digest_t),SEEK_SET)!=0) {
        fclose(file);return false;
    }
    stored_digest_t d;
    bool ok=fread(&d,sizeof(d),1,file)==1 &&
        d.magic==DIGEST_MAGIC && (d.version==1 || (d.version&0x8000u)) &&
        d.checksum==digest_hash(&d) && memchr(d.text,0,sizeof(d.text));
    fclose(file);
    if(ok) {
        memset(out,0,sizeof(*out));
        out->processed_through=d.processed_through;
        out->included=d.included;
        out->day_tag=(d.version&0x8000u)?d.version:0;
        memcpy(out->summary,d.text,sizeof(out->summary));
    }
    return ok;
}


/* Idempotent recovery for a crash between a durable summary write and the
 * independent NVS cursor update. Compaction has already rebased the summary
 * checkpoint (or replayed its transaction marker) before this function runs. */
bool hub_archive_reconcile_cursor(hub_archive_t *db) {
    if(!db || !db->mounted || db->failed)return false;
    uint32_t cursor=hub_ai_read_cursor();
    if(cursor>db->rows)return false;
    hub_ai_digest_t *state=&s_archive_digest_scratch;
    uint32_t version=0;
    if(!latest_active(state,NULL,&version) || version!=ACTIVE_VERSION)
        return true; /* v2 lacks compaction rebasing: never skip new records */
    if(state->processed_through>cursor &&
       state->processed_through<=db->rows) {
        ESP_LOGW(TAG,"Recovering durable AI commit %lu -> %lu",
                 (unsigned long)cursor,(unsigned long)state->processed_through);
        return hub_ai_set_cursor(state->processed_through);
    }
    return true;
}
uint32_t hub_archive_digest_revision(hub_archive_t *db) {
    uint32_t gen=0;
    if(db && db->mounted && !db->failed)(void)latest_active(NULL,&gen,NULL);
    return gen;
}
bool hub_archive_last_digest(hub_archive_t *db,hub_ai_digest_t *out) {
    if(!db || !db->mounted || db->failed || !out)return false;
    if(latest_active(out,NULL,NULL))return !out->cleared && out->summary[0];
    return hub_archive_legacy_digest(db,out);
}
bool hub_archive_hide_digest(hub_archive_t *db) {
    hub_ai_digest_t *d=&s_archive_digest_scratch;
    if(!hub_archive_last_digest(db,d))return false;
    if(d->hidden)return true;
    d->hidden=true;
    return write_active(d);
}
bool hub_archive_show_digest(hub_archive_t *db) {
    hub_ai_digest_t *d=&s_archive_digest_scratch;
    if(!hub_archive_last_digest(db,d))return false;
    if(!d->hidden)return true;
    d->hidden=false;
    return write_active(d);
}
/* Per-item completion is a compare-and-swap update of the active digest.
 * The caller must supply the exact revision visible on its web form.
 * It never alters the notification checkpoint or the rest of the tasks. */
bool hub_archive_complete_task(hub_archive_t *db,uint32_t fingerprint,
                               uint32_t expected_revision) {
    if(!db || !db->mounted || db->failed || !fingerprint ||
       expected_revision==0)return false;
    hub_ai_digest_t *d=&s_archive_digest_scratch;
    uint32_t current=0;
    if(!latest_active(d,&current,NULL) || current!=expected_revision ||
       !hub_ai_complete_pending(d,fingerprint))return false;
    /* Keep both checksum slots on the completed state so a torn newest slot
     * cannot resurrect a task. This is a rare user action, not the hot path. */
    if(!write_active(d))return false;
    return write_active(d);
}
bool hub_archive_ack_digest(hub_archive_t *db) {
    if(!db || !db->mounted || db->failed)return false;
    hub_ai_digest_t *d=&s_archive_digest_scratch;
    memset(d,0,sizeof(*d));
    (void)hub_archive_last_digest(db,d);
    memset(d->summary,0,sizeof(d->summary));
    memset(d->tasks,0,sizeof(d->tasks));
    d->task_count=0;d->included=0;d->cleared=true;d->hidden=false;
    /* Acknowledgement ends this entire epoch, including up to two previews
     * still waiting for their first trigger. They stay in the raw archive
     * until GC but never feed the next epoch's three-notice threshold. */
    d->processed_through=db->rows;
    /* Two durable tombstones prevent a torn slot resurrecting old tasks.
     * If power fails before NVS checkpoint, startup reconciliation restores
     * the cursor from the newest persisted tombstone. */
    if(!write_active(d) || !write_active(d))return false;
    return hub_ai_advance_cursor(d->processed_through);
}
bool hub_archive_initialize(hub_archive_t *db,bool confirmed) {
    if(!confirmed || !db || !db->failed || db->mounted) return false;
    const esp_partition_t *p=esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_FAT,"archive");
    if(!p || p->address!=0x400000 || p->size!=0x400000) return false;
    uint32_t session=db->session;
    if(db->file) {if(fclose(db->file)!=0) {db->file=NULL;return false;} db->file=NULL;}
    if(db->wl!=WL_INVALID_HANDLE) {
        esp_err_t err=db->volume_mounted?
            esp_vfs_fat_spiflash_unmount_rw_wl("/archive",db->wl):wl_unmount(db->wl);
        if(err!=ESP_OK) return false;
        db->wl=WL_INVALID_HANDLE;
        db->volume_mounted=false;
    }
    if(esp_partition_erase_range(p,0,p->size)!=ESP_OK) return false;
    return hub_archive_open(db,session);
}
bool hub_archive_clear(hub_archive_t *db,bool confirmed) {
    if(!confirmed || !db || !db->mounted || db->failed || !db->file ||
       !sync_file(db->file)) return false;
    const esp_partition_t *p=esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_FAT,"archive");
    if(!p || p->address!=0x400000 || p->size!=0x400000) return false;
    uint32_t session=db->session;
    if(fclose(db->file)!=0) {db->file=NULL;db->failed=true;return false;}
    db->file=NULL;
    if(esp_vfs_fat_spiflash_unmount_rw_wl("/archive",db->wl)!=ESP_OK) {
        db->failed=true;
        return false;
    }
    db->wl=WL_INVALID_HANDLE;
    db->volume_mounted=false;
    db->mounted=false;
    if(esp_partition_erase_range(p,0,p->size)!=ESP_OK) {
        db->failed=true;
        return false;
    }
    return hub_archive_open(db,session);
}
