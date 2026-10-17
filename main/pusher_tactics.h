#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CP_SPECIAL_COUNT 8
#define CP_SPECIAL_STOCK 4
#define CP_SPECIAL_QUEUE 32
#define CP_ACT_COUNT 5
/* Marks remain on physical bodies; no screen-space trigger substitutes for SAT. */
typedef enum {CP_SP_NONE,CP_SP_BLAST,CP_SP_MAGNET,CP_SP_SEED,CP_SP_WATER,
 CP_SP_PRISM,CP_SP_METEOR,CP_SP_GUARD,CP_SP_ECHO} cp_special_t;
#define CP_TRAIT_DOUBLE 1u
#define CP_TRAIT_CROWN 2u
#define CP_TRAIT_FIRED 4u
#define CP_TRAIT_WATERED 8u
#define CP_TRAIT_CHARGED 16u

typedef struct {
 const char *name,*line1,*line2,*rune;
} cp_special_info_t;
extern const cp_special_info_t cp_special_info[CP_SPECIAL_COUNT];
typedef struct {
 uint32_t casts,links,blasts,grown,stage_ticks,crown_retry;
 uint32_t stage_gross,stage_collected;
 uint16_t hopper_clock;uint32_t house_coins;
 uint8_t queue[CP_SPECIAL_QUEUE],queue_head,queue_count,contract,prize_stock;
 uint16_t used[CP_SPECIAL_COUNT];
 uint16_t fx_ticks,autocast_ticks,growth_ticks;
 int16_t fx_x,fx_y,growth_x,growth_y;
 uint8_t blocked_ticks;
 uint8_t stock[CP_SPECIAL_COUNT],selected,supply[3],stage,end_reason,fx_kind,growth_queue;
 bool active,crown_spawned,crown_collected,won;
} cp_tactics_t;
struct cp_game;
void cp_tactics_start(struct cp_game *g);
void cp_tactics_tick(struct cp_game *g);
void cp_tactics_touch(struct cp_game *g,unsigned a,unsigned b);
void cp_tactics_collect(struct cp_game *g,unsigned body);
void cp_tactics_lost(struct cp_game *g,unsigned body);
void cp_tactics_grant(struct cp_game *g,unsigned kind,unsigned count);
void cp_tactics_supply(struct cp_game *g,unsigned choice);
void cp_tactics_upgrade(struct cp_game *g,unsigned relic);
bool cp_tactics_cast(struct cp_game *g,unsigned kind);
unsigned cp_tactics_auto_kind(const struct cp_game *g);
unsigned cp_tactics_goal(const struct cp_game *g);
unsigned cp_tactics_earned(const struct cp_game *g);
unsigned cp_tactics_quota(const struct cp_game *g);
void cp_cabinet_load(struct cp_game *g);
unsigned cp_tactics_seconds(const struct cp_game *g);
bool cp_tactics_valid(const cp_tactics_t *t);

unsigned cp_tactics_relic_coin(unsigned relic);
