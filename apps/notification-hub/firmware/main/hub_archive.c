#include "hub_archive.h"
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
        memchr(r->body,0,sizeof(r->body));
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
    uint8_t buffer[4096];
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
static void record_pending(hub_archive_t *db,uint32_t uid,uint32_t session,uint32_t offset);
static void fold_preview(hub_archive_t *db,const hub_archive_record_t *preview);
static bool is_superseded(const hub_archive_t *db,uint32_t index);
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
        .max_files=2,
        .allocation_unit_size=4096,
    };
    esp_err_t err=esp_vfs_fat_spiflash_mount_rw_wl(
        "/archive","archive",&cfg,&db->wl);
    if(err!=ESP_OK) {
        db->failed=true;
        ESP_LOGE(TAG,"Archive mount failed; existing data preserved (%s)",
                 esp_err_to_name(err));
        return false;
    }
    db->file=fopen(HUB_ARCHIVE_PATH,"rb+");
    if(!db->file && errno==ENOENT) db->file=fopen(HUB_ARCHIVE_PATH,"wb+");
    if(!db->file) {db->failed=true;return false;}
    if(fseek(db->file,0,SEEK_END)!=0) {db->failed=true;return false;}
    long size=ftell(db->file);
    if(size<0) {db->failed=true;return false;}
    /* Incomplete tail: keep existing bytes and refuse further writing,
     * without silently truncating or erasing the history. */
    if((size % sizeof(hub_archive_record_t))!=0) {
        db->failed=true;
        ESP_LOGE(TAG,"Partial archive tail; refusing writes");
        return false;
    }
    db->rows=(uint32_t)size/sizeof(hub_archive_record_t);
    if(db->rows>=HUB_ARCHIVE_MAX_RECORDS) db->full=true;
    /* Limit so a full journal does not hit filesystem metadata exhaustion. */
    db->usable_bytes=p->size-32768;
    if((uint32_t)size+sizeof(hub_archive_record_t)>db->usable_bytes)
        db->full=true;
    if(fseek(db->file,0,SEEK_SET)!=0) {db->failed=true;return false;}
    hub_archive_record_t r;
    for(uint32_t i=0;i<db->rows;i++) {
        if(fread(&r,sizeof(r),1,db->file)!=1 || !valid(&r)) {
            db->failed=true;
            ESP_LOGE(TAG,"Corrupt archive record at slot %lu",(unsigned long)i);
            return false;
        }
        if(r.sequence>=db->next_sequence) db->next_sequence=r.sequence+1;
        if(r.obsolete) {db->failed=true;return false;}
        int idx=group_index(db,r.app,true);
        if(idx<0) {
            db->failed=true;
            ESP_LOGE(TAG,"Group index exhausted; archive remains untouched");
            return false;
        }
        db->groups[idx].count++;
        if(r.kind==HUB_ARCHIVE_SOURCE)
            record_pending(db,r.uid,r.session,i);
        else if(r.kind==HUB_ARCHIVE_PREVIEW)
            fold_preview(db,&r);
    }
    db->mounted=true;
    ESP_LOGI(TAG,"Archive mounted: %lu records, %u application groups",
             (unsigned long)db->rows,(unsigned)db->group_count);
    return true;
}
static bool is_superseded(const hub_archive_t *db,uint32_t index) {
    return index<HUB_ARCHIVE_MAX_RECORDS &&
        (db->superseded[index/8] & (uint8_t)(1u<<(index%8)))!=0;
}
static void record_pending(hub_archive_t *db,uint32_t uid,uint32_t session,
                           uint32_t offset) {
    hub_archive_pending_t *p=&db->pending[db->pending_cursor++ %
                                         (sizeof(db->pending)/sizeof(db->pending[0]))];
    *p=(hub_archive_pending_t){
        .uid=uid,.session=session,.offset=offset,.active=true
    };
}
static void fold_preview(hub_archive_t *db,
                         const hub_archive_record_t *preview) {
    for(size_t i=0;i<sizeof(db->pending)/sizeof(db->pending[0]);i++) {
        hub_archive_pending_t *p=&db->pending[i];
        if(!p->active || p->uid!=preview->uid ||
           p->session!=preview->session) continue;
        p->active=false;
        if(p->offset>=db->rows || is_superseded(db,p->offset)) break;
        db->superseded[p->offset/8] |= (uint8_t)(1u<<(p->offset%8));
        /* A source record is always stored in "Unresolved" group. */
        int group=group_index(db,"Unresolved",false);
        if(group>=0 && db->groups[group].count) db->groups[group].count--;
        break;
    }
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
    r.session=db->session;
    r.sequence=db->next_sequence++;
    r.elapsed_seconds=(uint32_t)(esp_timer_get_time()/1000000);
    r.obsolete=0;
    r.reserved=0;
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
        record_pending(db,r.uid,r.session,new_slot);
    else fold_preview(db,&r);
    return true;
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
    for(uint32_t i=db->rows;i>0;i--) {
        hub_archive_record_t r;
        if(!load_entry(db,i-1,&r)) {db->failed=true;return false;}
        if(!is_superseded(db,i-1) && strcmp(r.app,app)==0) {
            if(nth==0) {*out=r;return true;}
            nth--;
        }
    }
    return false;
}

#define DIGEST_MAGIC 0x534D4D59u
#define DIGEST_PATH "/archive/ai-digest.dat"
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
bool hub_archive_collect_since(hub_archive_t *db,uint32_t cursor,
                               hub_ai_batch_t *out) {
    if(!db || !db->mounted || db->failed || !out) return false;
    memset(out,0,sizeof(*out));
    out->after_sequence=cursor;
    for(uint32_t slot=0;slot<db->rows && out->count<HUB_AI_MAX_BATCH;slot++) {
        hub_archive_record_t r;
        if(!load_entry(db,slot,&r)) {db->failed=true;return false;}
        if(r.sequence<=cursor || is_superseded(db,slot)) continue;
        uint8_t i=out->count++;
        out->items[i].sequence=r.sequence;
        snprintf(out->items[i].app,sizeof(out->items[i].app),"%s",r.app);
        snprintf(out->items[i].title,sizeof(out->items[i].title),"%s",r.title);
        snprintf(out->items[i].body,sizeof(out->items[i].body),"%s",r.body);
        out->through_sequence=r.sequence;
    }
    return true;
}
bool hub_archive_save_digest(hub_archive_t *db,const hub_ai_digest_t *summary) {
    if(!db || !db->mounted || db->failed || !summary ||
       !summary->summary[0] || !memchr(summary->summary,0,sizeof(summary->summary)))
        return false;
    stored_digest_t d={.magic=DIGEST_MAGIC,
        .processed_through=summary->processed_through,
        .included=summary->included,.version=1};
    snprintf(d.text,sizeof(d.text),"%s",summary->summary);
    d.checksum=digest_hash(&d);
    FILE *file=fopen(DIGEST_PATH,"ab");
    if(!file) return false;
    bool ok=fwrite(&d,sizeof(d),1,file)==1 && sync_file(file);
    if(fclose(file)!=0) ok=false;
    if(!ok) ESP_LOGE(TAG,"AI digest couldn't be saved to Flash");
    return ok;
}
bool hub_archive_last_digest(hub_archive_t *db,hub_ai_digest_t *out) {
    if(!db || !db->mounted || !out) return false;
    FILE *file=fopen(DIGEST_PATH,"rb");
    if(!file) return false;
    if(fseek(file,0,SEEK_END)!=0) {fclose(file);return false;}
    long length=ftell(file);
    if(length<(long)sizeof(stored_digest_t) ||
       length%(long)sizeof(stored_digest_t)!=0 ||
       fseek(file,length-(long)sizeof(stored_digest_t),SEEK_SET)!=0) {
        fclose(file);return false;
    }
    stored_digest_t d;
    bool ok=fread(&d,sizeof(d),1,file)==1 &&
        d.magic==DIGEST_MAGIC && d.version==1 &&
        d.checksum==digest_hash(&d) && memchr(d.text,0,sizeof(d.text));
    fclose(file);
    if(ok) {
        memset(out,0,sizeof(*out));
        out->processed_through=d.processed_through;
        out->included=d.included;
        memcpy(out->summary,d.text,sizeof(out->summary));
    }
    return ok;
}
