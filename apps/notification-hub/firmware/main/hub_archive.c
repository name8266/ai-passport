#include "hub_archive.h"
#include <stddef.h>
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
    if(!create || db->group_count>=HUB_ARCHIVE_GROUP_LIMIT) return -1;
    uint16_t idx=db->group_count++;
    snprintf(db->groups[idx].app,sizeof(db->groups[idx].app),"%s",app);
    db->groups[idx].count=0;
    return idx;
}
static bool partition_is_blank(const esp_partition_t *p) {
    uint8_t buffer[4096];
    if(!p || esp_partition_read(p,0,buffer,sizeof(buffer))!=ESP_OK) return false;
    for(size_t i=0;i<sizeof(buffer);i++) if(buffer[i]!=0xFF) return false;
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
    db->file=fopen(HUB_ARCHIVE_PATH,"ab+");
    if(!db->file) {db->failed=true;return false;}
    if(fseek(db->file,0,SEEK_END)!=0) {db->failed=true;return false;}
    long size=ftell(db->file);
    if(size<0) {db->failed=true;return false;}
    /* Incomplete tail: keep existing bytes, but never append over corruption.
     * A later recovery tool may salvage the valid prefix. */
    if((size % sizeof(hub_archive_record_t))!=0) {
        db->failed=true;
        ESP_LOGE(TAG,"Partial archive tail; refusing writes");
        return false;
    }
    db->rows=(uint32_t)size/sizeof(hub_archive_record_t);
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
        if(r.obsolete) continue;
        int idx=group_index(db,r.app,true);
        if(idx>=0) db->groups[idx].count++;
    }
    db->mounted=true;
    ESP_LOGI(TAG,"Archive mounted: %lu records, %u application groups",
             (unsigned long)db->rows,(unsigned)db->group_count);
    return true;
}
static bool update_superseded(hub_archive_t *db,uint32_t idx,
                              const hub_archive_record_t *newest) {
    if(idx>=db->rows) return false;
    hub_archive_record_t old;
    if(!load_entry(db,idx,&old)) return false;
    if(old.kind!=HUB_ARCHIVE_SOURCE || old.uid!=newest->uid ||
       old.session!=newest->session || old.obsolete) return false;
    old.obsolete=1;
    old.checksum=checksum(&old);
    if(fseek(db->file,(long)idx*sizeof(old),SEEK_SET)!=0 ||
       fwrite(&old,sizeof(old),1,db->file)!=1 || !sync_file(db->file))
        return false;
    int group=group_index(db,old.app,false);
    if(group>=0 && db->groups[group].count)
        db->groups[group].count--;
    return true;
}
bool hub_archive_capture(hub_archive_t *db,const hub_archive_record_t *input) {
    if(!db || !db->mounted || db->full || db->failed || !input) return false;
    if(input->kind!=HUB_ARCHIVE_SOURCE && input->kind!=HUB_ARCHIVE_PREVIEW)
        return false;
    if((db->rows+1u)*sizeof(hub_archive_record_t)>db->usable_bytes) {
        db->full=true;
        return false;
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
    if(group>=0) db->groups[group].count++;
    if(r.kind==HUB_ARCHIVE_SOURCE) {
        hub_archive_pending_t *pending=&db->pending[db->pending_cursor++ %
                                                     (sizeof(db->pending)/sizeof(db->pending[0]))];
        *pending=(hub_archive_pending_t){.uid=r.uid,.offset=new_slot,.active=true};
    } else {
        /* Preview is fully committed before editing the pending source marker.
         * A crash between these writes may leave one duplicate placeholder,
         * but does not discard the committed preview. */
        for(size_t i=0;i<sizeof(db->pending)/sizeof(db->pending[0]);i++) {
            hub_archive_pending_t *p=&db->pending[i];
            if(p->active && p->uid==r.uid) {
                (void)update_superseded(db,p->offset,&r);
                p->active=false;
                break;
            }
        }
    }
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
        if(!r.obsolete && strcmp(r.app,app)==0) {
            if(nth==0) {*out=r;return true;}
            nth--;
        }
    }
    return false;
}
