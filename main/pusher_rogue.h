#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CP_RELIC_COUNT 28
#define CP_RELIC_MAX 3
#define CP_RUN_COINS 40
#define CP_RUN_GRACE 600
#define CP_CHIP_LIMIT 9999999u
typedef enum {
 CP_R_MAGNET,CP_R_MOON,CP_R_ICE,CP_R_BOUNCE,CP_R_ROD,CP_R_WAVE,CP_R_ECHO,
 CP_R_RIPPLE,CP_R_WORM,CP_R_FUSION,CP_R_FOLD,CP_R_ALCHEMY,CP_R_RAINBOW,
 CP_R_LUCK,CP_R_TIME,CP_R_COMBO,CP_R_INTEREST,CP_R_THRIFT,CP_R_EXCHANGE,
 CP_R_INSURANCE,CP_R_SUPPLY,CP_R_SHOP,CP_R_MAP,CP_R_MIRROR,CP_R_THUNDER,
 CP_R_FLOAT,CP_R_METEOR,CP_R_FATE
} cp_relic_id_t;
typedef enum {CP_VIEW_PLAY,CP_VIEW_DRAFT,CP_VIEW_PAUSE,CP_VIEW_RANK,
 CP_VIEW_CODEX,CP_VIEW_SHOP,CP_VIEW_END,CP_VIEW_TRAY,CP_VIEW_SUPPLY} cp_view_t;
typedef struct {const char *name,*line1,*line2;} cp_relic_info_t;
extern const cp_relic_info_t cp_relic_info[CP_RELIC_COUNT];
extern const char *const cp_synergy_names[8];
typedef struct {
 uint32_t seed,discovered,gross,revision,synergies;
 uint16_t xp,goal,level,grace,combo_ticks,storm_ticks,float_ticks,ripple_ticks,toast_ticks;
 uint16_t exchange_progress,gold_progress,map_progress,fusion_progress,meteor_progress;
 uint16_t combo_progress,interest_clock;uint8_t supply_progress,seal_progress,prize_credit;
 int16_t ripple_x,ripple_y;
 uint8_t relic[CP_RELIC_COUNT],offer[3],cursor,view,back,inspect,rank,rank_back;
 uint8_t pending,rerolls,shop_uses,insurance,combo,chip_queue,prize_queue,spawn_clock;
 uint8_t gravity,friction,bounce,stroke,paid_progress,event_progress;
 bool active,practice,finished,recorded,store_failed,muted;
} cp_rogue_t;
struct cp_game;
void cp_rogue_start(struct cp_game *g,uint32_t discovered);
void cp_rogue_choose(struct cp_game *g,unsigned choice);
bool cp_rogue_reroll(struct cp_game *g);
void cp_rogue_grant(struct cp_game *g,unsigned id); /* Diagnostic; caller marks practice. */
void cp_rogue_view(struct cp_game *g,cp_view_t view);
void cp_rogue_finish(struct cp_game *g);
void cp_rogue_paid(struct cp_game *g);
void cp_rogue_collect(struct cp_game *g,unsigned kind,bool siphon,int x,int y,unsigned value);
void cp_rogue_lost(struct cp_game *g);
void cp_rogue_jackpot(struct cp_game *g);
void cp_rogue_event(struct cp_game *g);
void cp_rogue_reel(struct cp_game *g);
void cp_rogue_tick(struct cp_game *g);
unsigned cp_rogue_duration(const struct cp_game *g,unsigned old,unsigned base);
bool cp_rogue_lane(const struct cp_game *g,unsigned lane);
bool cp_rogue_buy(struct cp_game *g);
unsigned cp_rogue_price(const struct cp_game *g);
bool cp_rogue_valid(const cp_rogue_t *r);

/* UP=0, DOWN=1, OK=2. Return owner action; menus are pure state. */
typedef enum {CP_CMD_NONE,CP_CMD_DROP,CP_CMD_BURST,CP_CMD_NEW,CP_CMD_SPECIAL,CP_CMD_SOUND} cp_rogue_cmd_t;
cp_rogue_cmd_t cp_rogue_key(struct cp_game *g,unsigned key,bool hold);

uint32_t cp_relic_synergies(const uint8_t levels[CP_RELIC_COUNT]);

void cp_rogue_effect(unsigned id,unsigned level,char *out,unsigned size);
