// Avida-inspired digital organisms: bounded, deterministic HEADS virtual CPU.
// Design source: Ofria & Wilke (2004), Artificial Life 10(2), 191-229.
// Not bit-for-bit compatible with official Avida; documented differences apply.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define AV_SIDE 16u
#define AV_SITES 256u
#define AV_CODE_MAX 64u
#define AV_CODE_MIN 10u
#define AV_ANCESTOR_LEN 15u
#define AV_EVENT_CAP 12u
#define AV_SAVE_MAGIC 0x41564932u
#define AV_SAVE_VERSION 2u

typedef enum {
    AV_NOP_A=0, AV_NOP_B, AV_NOP_C, AV_IF_N_EQU, AV_IF_LESS, AV_SWAP,
    AV_POP, AV_PUSH, AV_SWAP_STK, AV_SHIFT_R, AV_SHIFT_L, AV_INC,
    AV_DEC, AV_ADD, AV_SUB, AV_NAND, AV_IO,
    AV_H_ALLOC, AV_H_DIVIDE, AV_H_COPY, AV_H_SEARCH,
    AV_MOV_HEAD, AV_JMP_HEAD, AV_GET_HEAD, AV_IF_LABEL, AV_SET_FLOW,
    AV_OPCODE_COUNT
} av_opcode_t;

typedef enum {
    AV_EVENT_NONE=0, AV_EVENT_BIRTH=1, AV_EVENT_MUTATION=2,
    AV_EVENT_REPLACE=3, AV_EVENT_FAIL=4
} av_event_kind_t;

typedef struct {
    uint8_t kind, site;
    uint16_t length;
    uint32_t organism_id, parent_id, lineage;
    uint32_t at_update;
} av_event_t;

// Genomes encode executable instructions. Daughter staging is per-organism
// to permit fair interleaving: no outside function auto-copies entire genomes.
typedef struct {
    uint8_t code[AV_CODE_MAX], daughter[AV_CODE_MAX];
    uint8_t length, ip, read_head, write_head, flow_head;
    uint8_t allocated, copied, executed_count;
    uint8_t executed_bits[AV_CODE_MAX/8u];
    uint8_t last_nops[8], last_nop_count;
    uint8_t active_stack, sp[2];
    uint8_t bonus, reserved;
    int32_t reg[3], stack[2][4];
    uint32_t id, parent_id, lineage, born_at;
    uint32_t instructions, offspring;
} av_organism_t;

typedef struct {
    uint32_t seed, rng;
    uint32_t instructions, updates, births, replacements, mutations;
    uint32_t insertion_deletions, copy_attempts, failed_divides;
    uint32_t next_id, next_lineage;
    uint16_t cursor;
    uint8_t mutation_level, paused;
    uint8_t event_head, event_count;
    av_event_t events[AV_EVENT_CAP];
    av_organism_t organisms[AV_SITES];
} av_world_t;

typedef struct {
    uint32_t magic, version, sequence;
    av_world_t world;
    uint32_t checksum;
} av_snapshot_t;

typedef struct {
    uint16_t population, genotype_count, genotype_length;
    uint16_t dominant_count, mutants_alive;
    uint32_t dominant_hash;
    uint32_t last_birth, last_mutation;
} av_stats_t;

void av_reset(av_world_t *world, uint32_t seed);
void av_step(av_world_t *world, uint32_t instruction_budget);
void av_stats(const av_world_t *world, av_stats_t *stats);
uint32_t av_genome_hash(const av_organism_t *organism);
void av_instruction_name(uint8_t code, char *output, size_t size);
const char *av_event_name(uint8_t kind);
bool av_get_ancestor(uint8_t program[AV_ANCESTOR_LEN]);

void av_snapshot_encode(const av_world_t *world, uint32_t sequence, av_snapshot_t *dst);
bool av_snapshot_decode(av_world_t *world, const av_snapshot_t *src);
