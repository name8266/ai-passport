#define _POSIX_C_SOURCE 200809L
#include "battery_archive.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ARCHIVE_MAGIC 0x42415232u
typedef struct {
    uint32_t magic,schema,revision,next_id,total,migrated,migration_events,counts[BAT_STATUS_COUNT];
    care_pet_t pet;
    uint32_t checksum;
} archive_meta_t;
typedef struct { bat_event_t event; uint32_t reward,checksum; } archive_event_t;
typedef struct { uint32_t magic,deleted; care_asset_t asset; archive_event_t event; archive_meta_t meta; uint32_t checksum; } archive_tx_t;
static archive_meta_t s_meta;
static char s_root[128];
static bool s_ready;
#ifdef BAT_ARCHIVE_TEST
int battery_archive_fail_stage;
#define FAIL_STAGE(n) do { if (battery_archive_fail_stage==(n)) { s_ready=false; return false; } } while(0)
#else
#define FAIL_STAGE(n) ((void)0)
#endif
static uint32_t crc(const void *data,size_t bytes) {
    const unsigned char *p=data; uint32_t c=0xffffffff;
    for (size_t i=0;i<bytes;++i) { c^=p[i]; for(unsigned j=0;j<8;++j)c=(c>>1)^(0xedb88320u&(0u-(c&1u))); }
    return ~c;
}
#define SEAL(o) ((o).checksum=crc(&(o),offsetof(__typeof__(o),checksum)))
#define VALID(o) ((o).checksum==crc(&(o),offsetof(__typeof__(o),checksum)))
static void path(char out[160],const char *name) { snprintf(out,160,"%s/%s",s_root,name); }
static bool read_object(const char *name,void *out,size_t size) {
    char p[160];path(p,name);FILE *f=fopen(p,"rb");if(!f)return false;
    bool ok=fread(out,1,size,f)==size && fgetc(f)==EOF;fclose(f);return ok;
}
static bool durable_write(const char *name,const void *data,size_t size) {
    char p[160],tmp[160];path(p,name);path(tmp,"replace.tmp");FILE *f=fopen(tmp,"wb");if(!f)return false;
    bool ok=fwrite(data,1,size,f)==size && fflush(f)==0 && fsync(fileno(f))==0;
    if(fclose(f)!=0)ok=false;
    if(ok)ok=rename(tmp,p)==0;
    return ok;
}
static void asset_name(uint32_t id,char out[24]) { snprintf(out,24,"a%08lx",(unsigned long)id); }
bool battery_archive_get(uint32_t id,care_asset_t *a) {
    char name[24];asset_name(id,name);
    return s_ready && read_object(name,a,sizeof(*a)) && VALID(*a) && a->asset.id==id && bat_asset_valid(&a->asset);
}
static bool apply(const archive_tx_t *t) {
    char name[24],p[160];asset_name(t->asset.asset.id,name);
    if(t->asset.asset.id) {
        if(t->deleted) { path(p,name);if(unlink(p)!=0 && errno!=ENOENT)return false; }
        else if(!durable_write(name,&t->asset,sizeof(t->asset)))return false;
    }
    FAIL_STAGE(2);
    path(p,"history");FILE *f=fopen(p,"r+b");if(!f)f=fopen(p,"w+b");if(!f)return false;
    long offset=(long)(t->event.event.sequence-1)*sizeof(t->event);
    bool ok=fseek(f,0,SEEK_END)==0;long size=ftell(f);
    if(size<offset)ok=false;
    if(ok && size>=offset+(long)sizeof(t->event)) {
        archive_event_t old;ok=fseek(f,offset,SEEK_SET)==0 && fread(&old,1,sizeof(old),f)==sizeof(old) && !memcmp(&old,&t->event,sizeof(old));
    } else if(ok) {
        ok=ftruncate(fileno(f),offset)==0 && fseek(f,offset,SEEK_SET)==0
            && fwrite(&t->event,1,sizeof(t->event),f)==sizeof(t->event) && fflush(f)==0 && fsync(fileno(f))==0;
    }
    if(fclose(f)!=0)ok=false;
    if(!ok)return false;
    FAIL_STAGE(3);
    if(!durable_write("meta",&t->meta,sizeof(t->meta)))return false;
    FAIL_STAGE(4);
    path(p,"pending");if(unlink(p)!=0 && errno!=ENOENT)return false;
    s_meta=t->meta;return true;
}
bool battery_archive_open(const char *root) {
    s_ready=false;if(strlen(root)>=sizeof(s_root))return false;strcpy(s_root,root);
    char p[160];path(p,"pending");struct stat st;
    if(stat(p,&st)==0) {
        archive_tx_t t;
        if(!read_object("pending",&t,sizeof(t)) || t.magic!=ARCHIVE_MAGIC || !VALID(t) || !VALID(t.asset) || !VALID(t.meta) || !VALID(t.event) || !apply(&t))return false;
    }
    path(p,"meta");
    if(stat(p,&st)==0) {
        if(!read_object("meta",&s_meta,sizeof(s_meta)) || s_meta.magic!=ARCHIVE_MAGIC || s_meta.schema!=1 || !VALID(s_meta))return false;
    } else {
        if(errno!=ENOENT)return false;
        /* Refuse to initialize over an unrecognized nonempty store. */
        DIR *d=opendir(root);if(!d)return false;struct dirent *e;bool empty=true;
        while((e=readdir(d)))if(e->d_name[0]!='.' && strcmp(e->d_name,"replace.tmp"))empty=false;
        closedir(d);if(!empty)return false;
        memset(&s_meta,0,sizeof(s_meta));s_meta.magic=ARCHIVE_MAGIC;s_meta.schema=1;s_meta.next_id=1;
        s_meta.pet.volume=20;s_meta.pet.quiet_start=22;s_meta.pet.quiet_end=8;SEAL(s_meta);
        if(!durable_write("meta",&s_meta,sizeof(s_meta)))return false;
    }
    s_ready=true;return true;
}
static bool commit(archive_tx_t *t) {
    if(!s_ready || s_meta.revision==UINT32_MAX)return false;
    t->magic=ARCHIVE_MAGIC;t->meta.magic=ARCHIVE_MAGIC;t->meta.schema=1;t->meta.revision=s_meta.revision+1;
    t->event.event.sequence=t->meta.revision;
    SEAL(t->asset);SEAL(t->event);SEAL(t->meta);SEAL(*t);
    if(!durable_write("pending",t,sizeof(*t)))return false;
    FAIL_STAGE(1);
    if(!apply(t)) { s_ready=false;return false; } return true;
}
static bool contains(const char *text,const char *q) {
    if(!q || !*q)return true;
    for(;*text;++text) { const unsigned char *a=(const unsigned char *)text,*b=(const unsigned char *)q;
        while(*a && *b) { unsigned ca=*a,cb=*b;if(ca>='A'&&ca<='Z')ca+=32;if(cb>='A'&&cb<='Z')cb+=32;if(ca!=cb)break;++a;++b; }
        if(!*b)return true;
    }return false;
}
bool battery_archive_page(bat_db_t *page,care_info_t *info,uint32_t after,const char *q,int filter,int64_t now) {
    if(!s_ready)return false;
    bat_init(page);memset(info,0,sizeof(*info));page->revision=s_meta.revision;page->next_id=s_meta.next_id;
    info->total=s_meta.total;info->revision=s_meta.revision;info->pet=s_meta.pet;info->page_after=after;
    memcpy(info->counts,s_meta.counts,sizeof(info->counts));
    unsigned matches=0,seen=0;uint32_t counts[BAT_STATUS_COUNT]={0};
    DIR *d=opendir(s_root);if(!d)return false;struct dirent *e;
    while((e=readdir(d))) {
        if(strlen(e->d_name)!=9 || e->d_name[0]!='a')continue;
        unsigned long parsed;char extra;if(sscanf(e->d_name,"a%8lx%c",&parsed,&extra)!=1)continue;
        care_asset_t a;if(!battery_archive_get((uint32_t)parsed,&a)) { s_ready=false;closedir(d);return false; }
        ++seen;++counts[a.asset.status];
        int64_t due=0;care_reason_t reason=care_due(&a,now,&due);
        bool active=reason!=CARE_NONE && (!due || (now && due<=now)) && (!a.snooze_until || (now && a.snooze_until<=now));
        if(active) {
            ++info->due_count;
            unsigned n=info->reminder_count;
            if(n<CARE_REMINDERS)info->reminders[info->reminder_count++]=(care_reminder_t){a.asset.id,due,(uint8_t)reason};
        }
        if(a.asset.status!=BAT_RETIRED && (a.asset.soc<=20 || a.asset.health<80 || a.asset.status==BAT_SERVICE))++info->attention;
        char code[24];snprintf(code,sizeof(code),"BAT-%03lu",parsed);
        if(a.asset.id<=after || (filter==-2 && !(a.asset.status!=BAT_RETIRED && (a.asset.soc<=20 || a.asset.health<80 || a.asset.status==BAT_SERVICE))) || (filter==-3 && !active) || (filter>=0 && a.asset.status!=filter)
            || (!contains(a.asset.name,q) && !contains(a.asset.location,q) && !contains(code,q)))continue;
        ++matches;
        unsigned pos=0;while(pos<page->count && page->assets[pos].id<a.asset.id)++pos;
        if(pos>=BAT_PAGE_SIZE)continue;
        if(page->count<BAT_PAGE_SIZE)++page->count;
        memmove(page->assets+pos+1,page->assets+pos,(page->count-pos-1)*sizeof(bat_asset_t));page->assets[pos]=a.asset;
    }
    closedir(d);
    if(seen!=s_meta.total || memcmp(counts,s_meta.counts,sizeof(counts))){s_ready=false;return false;}
    if(matches>BAT_PAGE_SIZE)info->next_cursor=page->assets[page->count-1].id;
    char p[160];path(p,"history");FILE *f=fopen(p,"rb");
    if(f) {
        unsigned count=s_meta.revision<BAT_MAX_EVENTS ? s_meta.revision : BAT_MAX_EVENTS;
        if(fseek(f,(long)(s_meta.revision-count)*sizeof(archive_event_t),SEEK_SET)!=0){fclose(f);return false;}
        for(unsigned i=0;i<count;++i) { archive_event_t event;
            if(fread(&event,1,sizeof(event),f)!=sizeof(event) || !VALID(event)){fclose(f);return false;}
            page->events[page->event_count++]=event.event;
        }fclose(f);
    } else if(s_meta.revision){s_ready=false;return false;}
    bat_seal(page);return true;
}
bat_result_t battery_archive_upsert(const bat_asset_t *asset,int64_t remind,unsigned minutes,uint32_t rev,int64_t now,int tz) {
    if(!s_ready || !bat_asset_valid(asset) || (remind && (remind<BAT_MIN_EPOCH || remind>BAT_MAX_EPOCH)) || minutes<1 || minutes>1440)return BAT_INVALID;
    if(rev!=s_meta.revision)return BAT_CONFLICT;
    archive_tx_t t={0};t.meta=s_meta;t.asset.asset=*asset;
    unsigned action=BAT_CREATE,from=asset->status;bool changed=false;
    if(!asset->id) {
        if(s_meta.next_id==UINT32_MAX)return BAT_FULL;
        t.asset.asset.id=t.meta.next_id++;++t.meta.total;++t.meta.counts[asset->status];
    } else {
        care_asset_t old;if(!battery_archive_get(asset->id,&old))return BAT_NOT_FOUND;
        if(old.asset.status!=asset->status)return BAT_TRANSITION;
        changed=memcmp(&old.asset,asset,sizeof(*asset))!=0;t.asset=old;t.asset.asset=*asset;action=BAT_EDIT;
    }
    t.asset.updated_at=now;t.asset.remind_at=remind;t.asset.charge_minutes=minutes;
    if(asset->status==BAT_CHARGING && !t.asset.charge_until && now)t.asset.charge_until=now+(int64_t)minutes*60;
    t.event.reward=care_reward(&t.meta.pet,&t.asset,(bat_action_t)action,changed,now,tz);
    t.event.event=(bat_event_t){.epoch=now,.asset_id=t.asset.asset.id,.action=action,.from_status=from,.to_status=asset->status};
    return commit(&t) ? BAT_OK : BAT_FULL;
}
bat_result_t battery_archive_action(uint32_t id,bat_action_t action,uint32_t rev,int64_t now,int tz) {
    if(!s_ready)return BAT_INVALID;
    if(rev!=s_meta.revision)return BAT_CONFLICT;
    archive_tx_t t={0};t.meta=s_meta;if(!battery_archive_get(id,&t.asset))return BAT_NOT_FOUND;
    bat_db_t *one=malloc(sizeof(*one));if(!one)return BAT_INVALID;bat_init(one);
    one->count=1;one->assets[0]=t.asset.asset;one->next_id=id+1;
    unsigned from=t.asset.asset.status;
    bat_result_t result=bat_action(one,id,action,0,now);
    if(result!=BAT_OK){free(one);return result;}
    if(action==BAT_DELETE) { t.deleted=1;--t.meta.total;--t.meta.counts[from]; }
    else {
        t.asset.asset=one->assets[0];--t.meta.counts[from];++t.meta.counts[t.asset.asset.status];
        if(action==BAT_CHARGE)t.asset.charge_until=now ? now+(t.asset.charge_minutes ? t.asset.charge_minutes : 120)*60 : 0;
        if(action==BAT_FINISH)t.asset.charge_until=0;
        if(action==BAT_RELEASE)t.asset.remind_at=0;
        t.asset.updated_at=now;
    }
    free(one);t.event.reward=care_reward(&t.meta.pet,&t.asset,action,true,now,tz);
    t.event.event=(bat_event_t){.epoch=now,.asset_id=id,.action=action,.from_status=from,.to_status=t.asset.asset.status};
    return commit(&t) ? BAT_OK : BAT_FULL;
}
bool battery_archive_snooze(int64_t now) {
    if(!s_ready || !now)return false;
    archive_tx_t t={0};t.meta=s_meta;t.meta.pet.snooze_until=now+15*60;
    t.event.event=(bat_event_t){.epoch=now,.action=11};return commit(&t);
}
bool battery_archive_settings(unsigned volume,unsigned start,unsigned end,bool muted) {
    if(!s_ready || volume>60 || start>23 || end>23)return false;
    archive_tx_t t={0};t.meta=s_meta;t.meta.pet.volume=volume;t.meta.pet.quiet_start=start;t.meta.pet.quiet_end=end;t.meta.pet.muted=muted;
    t.event.event.action=12;return commit(&t);
}
bool battery_archive_migrate(const bat_db_t *old) {
    if(!s_ready)return false;
    if(s_meta.migrated)return true;
    /* IDs retained; source NVS remains intact. Repeated migration skips existing IDs. */
    for(unsigned i=0;i<old->count;++i) {
        care_asset_t existing;if(battery_archive_get(old->assets[i].id,&existing))continue;
        archive_tx_t t={0};t.meta=s_meta;t.asset.asset=old->assets[i];t.asset.charge_minutes=120;
        for(unsigned j=0;j<old->event_count;++j)if(old->events[j].asset_id==t.asset.asset.id && old->events[j].epoch>t.asset.updated_at)t.asset.updated_at=old->events[j].epoch;
        if(t.asset.asset.status==BAT_CHARGING && t.asset.updated_at)t.asset.charge_until=t.asset.updated_at+7200;
        if(t.asset.asset.id>=t.meta.next_id)t.meta.next_id=t.asset.asset.id+1;
        ++t.meta.total;++t.meta.counts[t.asset.asset.status];
        t.event.event=(bat_event_t){.asset_id=t.asset.asset.id,.action=BAT_CREATE,.from_status=t.asset.asset.status,.to_status=t.asset.asset.status};
        if(!commit(&t))return false;
    }
    for(unsigned i=s_meta.migration_events;i<old->event_count;++i) {
        archive_tx_t t={0};t.meta=s_meta;t.meta.migration_events=i+1;t.event.event=old->events[i];
        if(!commit(&t))return false;
    }
    archive_tx_t mark={0};mark.meta=s_meta;mark.meta.migrated=1;
    if(mark.meta.next_id<old->next_id)mark.meta.next_id=old->next_id;
    mark.event.event.action=13;
    return commit(&mark);
}

uint32_t battery_archive_previous(uint32_t after,int filter,int64_t now) {
    uint32_t ids[BAT_PAGE_SIZE]={0};unsigned count=0;
    DIR *d=opendir(s_root);if(!d)return 0;struct dirent *e;
    while((e=readdir(d))) {
        unsigned long id;char extra;
        if(strlen(e->d_name)!=9 || e->d_name[0]!='a' || sscanf(e->d_name,"a%8lx%c",&id,&extra)!=1 || id>after)continue;
        care_asset_t a;if(!battery_archive_get((uint32_t)id,&a)){closedir(d);return 0;}
        if(filter==-3) { int64_t due=0;care_reason_t reason=care_due(&a,now,&due);
            if(reason==CARE_NONE || (due && (!now || due>now)))continue;
        }
        unsigned pos=0;while(pos<count && ids[pos]>(uint32_t)id)++pos;if(pos>=BAT_PAGE_SIZE)continue;
        if(count<BAT_PAGE_SIZE)++count;
        memmove(ids+pos+1,ids+pos,(count-pos-1)*sizeof(uint32_t));ids[pos]=id;
    }
    closedir(d);return count ? ids[count-1]-1 : 0;
}

int battery_archive_history(bat_event_t *events,unsigned capacity,uint32_t after,uint32_t revision) {
    if(!s_ready || s_meta.revision!=revision || after>revision)return -1;
    unsigned count=revision-after;if(count>capacity)count=capacity;
    if(!count)return 0;
    char p[160];path(p,"history");FILE *f=fopen(p,"rb");if(!f)return -1;
    bool ok=fseek(f,(long)after*sizeof(archive_event_t),SEEK_SET)==0;
    for(unsigned i=0;i<count && ok;++i) {
        archive_event_t item;ok=fread(&item,1,sizeof(item),f)==sizeof(item) && VALID(item) && item.event.sequence==after+i+1;
        events[i]=item.event;
    }
    fclose(f);return ok ? (int)count : -1;
}

bool battery_archive_ready(void) { return s_ready; }

bool battery_archive_migrated(void) { return s_ready && s_meta.migrated; }
