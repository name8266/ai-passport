#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BAT_MAX_ASSETS 16
#define BAT_MAX_EVENTS 48
#define BAT_SCHEMA 1
#define BAT_MIN_EPOCH 1704067200LL
#define BAT_MAX_EPOCH 4102444800LL

typedef enum { BAT_READY, BAT_IN_USE, BAT_CHARGING, BAT_SERVICE, BAT_RETIRED, BAT_STATUS_COUNT } bat_status_t;
typedef enum { BAT_CREATE, BAT_EDIT, BAT_CHECKOUT, BAT_RETURN, BAT_CHARGE, BAT_FINISH, BAT_INSPECT, BAT_RETIRE, BAT_DELETE, BAT_RELEASE } bat_action_t;
typedef enum { BAT_OK, BAT_INVALID, BAT_NOT_FOUND, BAT_CONFLICT, BAT_FULL, BAT_TRANSITION } bat_result_t;

typedef struct {
    uint32_t id;
    uint16_t capacity_mah, cycles;
    uint8_t soc, health, status, chemistry;
    char name[64], location[48], notes[96];
} bat_asset_t;
typedef struct {
    int64_t epoch;
    uint32_t asset_id, sequence;
    uint8_t action, from_status, to_status, reserved;
} bat_event_t;
typedef struct {
    uint32_t magic, schema, revision, next_id, event_sequence;
    uint16_t count, event_count;
    bat_asset_t assets[BAT_MAX_ASSETS];
    bat_event_t events[BAT_MAX_EVENTS];
    uint32_t checksum;
} bat_db_t;
typedef struct { bool synced; int16_t timezone_minutes; int64_t epoch, monotonic_ms; } bat_clock_t;
typedef struct { uint16_t statuses[BAT_STATUS_COUNT]; uint32_t capacity_mah; uint16_t low, attention; } bat_summary_t;

void bat_init(bat_db_t *db);
bool bat_text_valid(const char *text, size_t capacity, bool required);
bool bat_asset_valid(const bat_asset_t *asset);
bool bat_db_valid(const bat_db_t *db);
void bat_seal(bat_db_t *db);
int bat_find(const bat_db_t *db, uint32_t id);
bat_result_t bat_upsert(bat_db_t *db, const bat_asset_t *asset, uint32_t revision, int64_t epoch);
bat_result_t bat_action(bat_db_t *db, uint32_t id, bat_action_t action, uint32_t revision, int64_t epoch);
void bat_summary(const bat_db_t *db, bat_summary_t *summary);
bool bat_clock_sync(bat_clock_t *clock, int64_t epoch, int timezone_minutes, int64_t monotonic_ms);
int64_t bat_clock_now(const bat_clock_t *clock, int64_t monotonic_ms);
const char *bat_status_name(unsigned status);
const char *bat_action_name(unsigned action);
