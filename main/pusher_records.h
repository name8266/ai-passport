#pragma once
#include "pusher_physics.h"
#include <stddef.h>
#define CP_RANK_COUNT 10
#define CP_RECORD_MAGIC 0x43505231u
typedef struct {
 uint32_t seed,chips,sequence;
 uint16_t paid,seconds;
 uint8_t level,build[7];
} cp_record_t;
typedef struct {
 uint32_t magic,discovered,next_sequence,generation;
 cp_record_t rows[CP_RANK_COUNT];
 uint32_t checksum;
} cp_records_t;
void cp_records_init(cp_records_t *b);
void cp_records_seal(cp_records_t *b);
bool cp_records_valid(const cp_records_t *b);
bool cp_records_add(cp_records_t *b,const cp_game_t *g);
unsigned cp_record_level(const cp_record_t *r,unsigned id);
