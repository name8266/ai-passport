#pragma once
/* Convex rigid bodies with position, orientation, linear and angular velocity.
 * Millimetres/seconds/grams. No platform dependencies or dynamic allocation. */
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "pusher_rogue.h"
#include "pusher_tactics.h"
#define CP_MAX_COINS 288
#define CP_MAX_CONTACTS 2304
#define CP_BOARD_WIDTH 208.0f
#define CP_FRONT_EDGE 145.0f
#define CP_SIDE_EDGE 120
#define CP_FEEDER_PEGS 13
#define CP_ENTRY_X 104
#define CP_ENTRY_Y 26
#define CP_ENTRY_Z 156
#define CP_ROD_X 86
#define CP_ROD_Z 130
#define CP_ROD_LENGTH 36
#define CP_HZ 30
#define CP_DT (1.0f/CP_HZ)
typedef struct { float x,y,z; } cp_v3;
typedef struct { float w,x,y,z; } cp_q4;
typedef enum { CP_COIN_COPPER, CP_COIN_GOLD, CP_PRIZE_RED,
               CP_PRIZE_BLUE, CP_PRIZE_GREEN, CP_CHIP } cp_coin_kind_t;
typedef struct {
    cp_v3 p,v,w;
    cp_q4 q;
    cp_v3 bound;
    float quiet;
    int16_t support[3];
    uint8_t kind,support_count,feeder,paid;
    uint8_t special,trait;uint16_t special_ticks;
    uint16_t feeder_mask;
    bool active,sleeping;
} cp_coin_t;
typedef enum {CP_EVENT_NONE,CP_EVENT_RUSH,CP_EVENT_RAIN,CP_EVENT_SAVE,CP_EVENT_JACKPOT,
    CP_EVENT_BLACKHOLE,CP_EVENT_DOUBLE,CP_EVENT_TOWERS,CP_EVENT_SHAKE,CP_EVENT_FEVER,
    CP_EVENT_MAX=CP_EVENT_FEVER} cp_event_t;
/* Pin contacts occupy bits 0..12 and 15. These two bits follow a body after exit. */
#define CP_BODY_SIPHON 0x2000u
#define CP_BODY_CLONE 0x4000u
#define CP_TOWER_TICKS (3*CP_HZ)
typedef struct {
    uint32_t spins,gate_hits,siphoned,clones,towers,tower_coins;
    uint16_t clock,spin_ticks,reveal_ticks,cooldown,play_ticks,bag;
    uint16_t hole_ticks,double_ticks,shake_ticks,fever_ticks,clone_clock;
    uint16_t tower_clock,tower_ticks;
    uint8_t lane,result,last_result,tower_side,next_side,tower_queue;
} cp_arcade_t;
static inline bool cp_kind_prize(unsigned kind){return kind>=CP_PRIZE_RED&&kind<=CP_PRIZE_GREEN;}
typedef struct cp_game {
    cp_coin_t coins[CP_MAX_COINS];
    uint32_t rng,bank,score,best,drops,collected,lost,prizes,round;
    float phase,pusher_y,pusher_v,game_time,accumulator,auto_clock;
    float payout_flash;
    uint32_t steps,overflow_steps;
    uint32_t rod_hits,peg_hits,feeder_exits,exit_bins[3];
    unsigned contacts,peak_contacts;
    float max_penetration;
    unsigned cache_hits,sat_calls;
    uint32_t prep_us,contact_us,solve_us,finish_us,ground_us,pairs_us,factory_us;
    uint32_t events,jackpots,bonus_drops,restocks,capacity_waits,circulation_cycles;
    uint16_t rush_ticks,guard_ticks,event_ticks,bonus_clock,restock_ticks[3],bonus_queue;
    uint8_t collection_mask,event_kind;
    cp_arcade_t arcade;
    cp_rogue_t rogue;
    cp_tactics_t tactics;
    bool circulation,features,paused,autoplay;
} cp_game_t;
/* Exact binary32 conversion of fixed-point fields without the software FPU.
 * Rounding is nearest/even when the integer needs more than 24 significant bits. */
static inline float cp_fixed_float(int32_t value,unsigned fraction){
    if(!value)return 0;
    uint32_t sign=value<0?0x80000000u:0,mag=value<0?0u-(uint32_t)value:(uint32_t)value;
    unsigned bit=31u-(unsigned)__builtin_clz(mag);uint32_t significand;
    if(bit<=23)significand=mag<<(23-bit);
    else{unsigned shift=bit-23;significand=mag>>shift;uint32_t tail=mag&((1u<<shift)-1),half=1u<<(shift-1);
        if(tail>half||(tail==half&&(significand&1)))++significand;
        if(significand==0x1000000u){significand>>=1;++bit;}}
    union {uint32_t bits;float value;} converted={.bits=sign|((bit+127-fraction)<<23)|(significand&0x7fffff)};
    return converted.value;
}
static inline cp_v3 cp_v(float x,float y,float z) { return (cp_v3){x,y,z}; }
static inline cp_v3 cp_add(cp_v3 a,cp_v3 b) { return cp_v(a.x+b.x,a.y+b.y,a.z+b.z); }
static inline cp_v3 cp_sub(cp_v3 a,cp_v3 b) { return cp_v(a.x-b.x,a.y-b.y,a.z-b.z); }
static inline cp_v3 cp_mul(cp_v3 a,float s) { return cp_v(a.x*s,a.y*s,a.z*s); }
static inline float cp_dot(cp_v3 a,cp_v3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static inline cp_v3 cp_cross(cp_v3 a,cp_v3 b) { return cp_v(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x); }
static inline cp_v3 cp_unit(cp_v3 a) { float n=cp_dot(a,a); return n>1e-12f?cp_mul(a,1.0f/sqrtf(n)):cp_v(0,0,1); }
static inline float cp_clamp(float x,float a,float b) { return x<a?a:x>b?b:x; }
void cp_init(cp_game_t *g,uint32_t seed,uint32_t bank,uint32_t best,uint32_t round);
typedef struct {int16_t x,z;} cp_peg_t;
extern const cp_peg_t cp_feeder_pegs[CP_FEEDER_PEGS];
/* Q8 coordinates/velocities of a rigid rod rotating about (86,26,130). */
typedef struct {int32_t x,z,vx,vz;} cp_rod_t;
void cp_feeder_rod(float seconds,cp_rod_t *rod);
void cp_game_rod(const cp_game_t *g,float seconds,cp_rod_t *rod);
bool cp_drop(cp_game_t *g);
/* Free inventory coin; identical physical inlet and gate path. */
bool cp_special_inlet(cp_game_t *g,unsigned kind);
/* House hopper uses a checked empty volume on the rear shelf. */
bool cp_house_coin(cp_game_t *g,int x,int y);
/* Apply a bounded Q8 velocity impulse; synchronizes/wakes the integer solver. */
void cp_body_kick(cp_game_t *g,unsigned i,int vx,int vy,int vz);
void cp_step(cp_game_t *g,float elapsed);
unsigned cp_coin_count(const cp_game_t *g);
unsigned cp_tower_count(const cp_game_t *g);
void cp_body_refresh(cp_coin_t *b);
/* Exact convex volume check for cabinet loading, not an AABB occupancy mask. */
bool cp_placement_clear(const cp_game_t *g,cp_coin_kind_t kind,cp_v3 p);
cp_v3 cp_body_axis(const cp_coin_t *b,unsigned axis);
cp_v3 cp_body_half(const cp_coin_t *b);
cp_v3 cp_body_inertia(const cp_coin_t *b);
float cp_body_inv_mass(const cp_coin_t *b);
cp_coin_t *cp_spawn(cp_game_t *g,cp_coin_kind_t kind,cp_v3 p,cp_q4 q);
cp_v3 cp_support(const cp_coin_t *b,cp_v3 direction);
bool cp_valid(const cp_game_t *g);
void cp_rebuild_support(cp_game_t *g);
void cp_set_clock(uint32_t (*clock_us)(void));

/* Q8 positions/half extents, Q12 axes. Returns false for an externally edited
 * or newly spawned pose until physics has synchronized its integer cache. */
typedef struct {int32_t p[3],q[4];int16_t axis[3][3],half[3];} cp_pose_fixed_t;
bool cp_pose_fixed(const cp_game_t *g,unsigned i,cp_pose_fixed_t *pose);

unsigned cp_prize_count(const cp_game_t *g);
bool cp_next_round(cp_game_t *g);

/* Exclusive frame scratch: physics and rendering share one owner task.
 * Valid only between cp_render_prepare() and the next physics operation. */
void *cp_frame_scratch(unsigned size);
/* Reserve the arena tail for CPU depth pixels during rendering only. Physics
 * may overwrite it between frames. Never submit this borrowed storage to DMA. */
void *cp_frame_depth_scratch(unsigned size);

/* Gameplay rewards have physical effects; bonuses use the same fixed inlet. */
void cp_reward_event(cp_game_t *g,cp_event_t event);

/* A real guided coin crossing the lit lane starts the arcade reels. */
void cp_arcade_gate(cp_game_t *g,unsigned lane);
bool cp_arcade_valid(const cp_arcade_t *arcade);
int cp_tower_pusher_mm(const cp_game_t *g);
