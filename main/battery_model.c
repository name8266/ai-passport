#include "battery_model.h"
#include <limits.h>
#include <string.h>

#define BAT_MAGIC 0x42415431u
static uint32_t checksum(const bat_db_t *db) {
    const uint8_t *p = (const uint8_t *)db;
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < offsetof(bat_db_t, checksum); ++i) {
        crc ^= p[i];
        for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
void bat_seal(bat_db_t *db) { db->checksum = checksum(db); }
void bat_init(bat_db_t *db) {
    memset(db, 0, sizeof(*db));
    db->magic = BAT_MAGIC; db->schema = BAT_SCHEMA; db->next_id = 1;
    bat_seal(db);
}
/* Strict bounded UTF-8, including rejection of controls, overlongs and surrogates. */
bool bat_text_valid(const char *text, size_t capacity, bool required) {
    size_t n = 0;
    while (n < capacity && text[n]) ++n;
    if (n == capacity || (required && !n)) return false;
    for (size_t i = 0; i < n;) {
        uint8_t c = (uint8_t)text[i++];
        if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; continue; }
        unsigned len; uint32_t cp, min;
        if (c >= 0xc2 && c <= 0xdf) { len=1; cp=c&31; min=0x80; }
        else if (c >= 0xe0 && c <= 0xef) { len=2; cp=c&15; min=0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { len=3; cp=c&7; min=0x10000; }
        else return false;
        if (i+len > n) return false;
        while (len--) { c=(uint8_t)text[i++]; if ((c&0xc0)!=0x80) return false; cp=(cp<<6)|(c&63); }
        if (cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
    }
    return true;
}
bool bat_asset_valid(const bat_asset_t *a) {
    return a->status < BAT_STATUS_COUNT && a->chemistry < 4 && a->soc <= 100 && a->health <= 100
        && a->capacity_mah >= 1 && a->capacity_mah <= 60000
        && bat_text_valid(a->name,sizeof(a->name),true)
        && bat_text_valid(a->location,sizeof(a->location),false)
        && bat_text_valid(a->notes,sizeof(a->notes),false);
}
int bat_find(const bat_db_t *db, uint32_t id) {
    for (unsigned i=0; i<db->count; ++i) if (db->assets[i].id==id) return (int)i;
    return -1;
}
bool bat_db_valid(const bat_db_t *db) {
    if (db->magic!=BAT_MAGIC || db->schema!=BAT_SCHEMA || db->count>BAT_PAGE_SIZE
        || db->event_count>BAT_MAX_EVENTS || !db->next_id || db->checksum!=checksum(db)) return false;
    for (unsigned i=0; i<db->count; ++i) {
        const bat_asset_t *a=&db->assets[i];
        if (!a->id || a->id>=db->next_id || !bat_asset_valid(a)) return false;
        for (unsigned j=0; j<i; ++j) if (db->assets[j].id==a->id) return false;
    }
    for (unsigned i=0; i<db->event_count; ++i) {
        const bat_event_t *e=&db->events[i];
        if (!e->asset_id || e->action>BAT_RELEASE || e->from_status>=BAT_STATUS_COUNT
            || e->to_status>=BAT_STATUS_COUNT || !e->sequence || e->sequence>db->event_sequence
            || (i && e->sequence<=db->events[i-1].sequence)
            || (e->epoch && (e->epoch<BAT_MIN_EPOCH || e->epoch>BAT_MAX_EPOCH))) return false;
    }
    return true;
}
static bool can_write(const bat_db_t *db, int64_t epoch) {
    return db->revision < UINT32_MAX && db->event_sequence < UINT32_MAX
        && (!epoch || (epoch>=BAT_MIN_EPOCH && epoch<=BAT_MAX_EPOCH));
}
static void record(bat_db_t *db, uint32_t id, unsigned action, unsigned from, unsigned to, int64_t epoch) {
    if (db->event_count==BAT_MAX_EVENTS) {
        memmove(db->events,db->events+1,(BAT_MAX_EVENTS-1)*sizeof(bat_event_t));
        --db->event_count;
    }
    bat_event_t *e=&db->events[db->event_count++];
    memset(e,0,sizeof(*e));
    e->asset_id=id; e->action=action; e->from_status=from; e->to_status=to;
    e->epoch=epoch; e->sequence=++db->event_sequence;
    ++db->revision; bat_seal(db);
}
bat_result_t bat_upsert(bat_db_t *db, const bat_asset_t *a, uint32_t revision, int64_t epoch) {
    if (revision!=db->revision) return BAT_CONFLICT;
    if (!bat_asset_valid(a) || !can_write(db,epoch)) return BAT_INVALID;
    int index=bat_find(db,a->id); unsigned from=a->status, action=BAT_EDIT;
    if (!a->id) {
        if (db->count==BAT_PAGE_SIZE || db->next_id==UINT32_MAX) return BAT_FULL;
        index=db->count++; action=BAT_CREATE;
    } else if (index<0) return BAT_NOT_FOUND;
    else {
        from=db->assets[index].status;
        /* State transitions are audited through the dedicated action API. */
        if (a->status!=from) return BAT_TRANSITION;
    }
    db->assets[index]=*a;
    if (!a->id) db->assets[index].id=db->next_id++;
    record(db,db->assets[index].id,action,from,a->status,epoch);
    return BAT_OK;
}
bat_result_t bat_action(bat_db_t *db, uint32_t id, bat_action_t action, uint32_t revision, int64_t epoch) {
    if (revision!=db->revision) return BAT_CONFLICT;
    if (!can_write(db,epoch)) return BAT_INVALID;
    int index=bat_find(db,id); if (index<0) return BAT_NOT_FOUND;
    bat_asset_t *a=&db->assets[index]; unsigned from=a->status, to=from;
    switch (action) {
        case BAT_CHECKOUT: if (from!=BAT_READY) return BAT_TRANSITION; to=BAT_IN_USE; break;
        case BAT_RETURN: if (from!=BAT_IN_USE) return BAT_TRANSITION; to=BAT_READY; break;
        case BAT_CHARGE: if (from!=BAT_READY && from!=BAT_SERVICE) return BAT_TRANSITION; to=BAT_CHARGING; break;
        case BAT_FINISH:
            if (from!=BAT_CHARGING || a->cycles==UINT16_MAX) return BAT_TRANSITION;
            to=BAT_READY; a->soc=100; ++a->cycles; break;
        case BAT_INSPECT: if (from!=BAT_READY) return BAT_TRANSITION; to=BAT_SERVICE; break;
        case BAT_RELEASE: if (from!=BAT_SERVICE) return BAT_TRANSITION; to=BAT_READY; break;
        case BAT_RETIRE: if (from==BAT_RETIRED) return BAT_TRANSITION; to=BAT_RETIRED; break;
        case BAT_DELETE:
            if (from!=BAT_RETIRED) return BAT_TRANSITION;
            memmove(a,a+1,(db->count-(unsigned)index-1)*sizeof(*a));
            memset(&db->assets[--db->count],0,sizeof(*a)); break;
        default: return BAT_INVALID;
    }
    if (action!=BAT_DELETE) a->status=to;
    record(db,id,action,from,to,epoch); return BAT_OK;
}
void bat_summary(const bat_db_t *db, bat_summary_t *s) {
    memset(s,0,sizeof(*s));
    for (unsigned i=0;i<db->count;++i) {
        const bat_asset_t *a=&db->assets[i]; ++s->statuses[a->status];
        if (a->status==BAT_RETIRED) continue;
        s->capacity_mah+=a->capacity_mah;
        if (a->soc<=20) ++s->low;
        if (a->soc<=20 || a->health<80 || a->status==BAT_SERVICE) ++s->attention;
    }
}
bool bat_clock_sync(bat_clock_t *c, int64_t epoch, int tz, int64_t mono) {
    if (epoch<BAT_MIN_EPOCH || epoch>BAT_MAX_EPOCH || tz< -720 || tz>840 || mono<0) return false;
    c->epoch=epoch; c->timezone_minutes=tz; c->monotonic_ms=mono; c->synced=true; return true;
}
int64_t bat_clock_now(const bat_clock_t *c, int64_t mono) {
    if (!c->synced || mono<c->monotonic_ms) return 0;
    int64_t seconds=(mono-c->monotonic_ms)/1000;
    if (seconds>BAT_MAX_EPOCH-c->epoch) return 0;
    return c->epoch+seconds;
}
const char *bat_status_name(unsigned s) {
    static const char *names[]={"Ready","In use","Charging","Service","Retired"};
    return s<BAT_STATUS_COUNT ? names[s] : "Unknown";
}
const char *bat_action_name(unsigned a) {
    static const char *names[]={"Created","Edited","Checked out","Returned","Charging","Charge done","Inspection","Retired","Deleted","Service done"};
    return a<=BAT_RELEASE ? names[a] : "Unknown";
}
