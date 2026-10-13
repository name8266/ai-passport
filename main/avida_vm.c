#include "avida_vm.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>

static const uint8_t s_ancestor[AV_ANCESTOR_LEN] = {
    AV_H_ALLOC, AV_H_SEARCH, AV_NOP_C, AV_NOP_A, AV_MOV_HEAD, AV_NOP_C,
    AV_H_SEARCH, AV_H_COPY, AV_IF_LABEL, AV_NOP_C, AV_NOP_A,
    AV_H_DIVIDE, AV_MOV_HEAD, AV_NOP_A, AV_NOP_B
};

/* Xorshift32 (Marsaglia, 2003): deterministic experiment PRNG, not cryptographic. */
static uint32_t random_u32(av_world_t *w)
{
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return w->rng = x;
}

static bool is_nop(uint8_t op) { return op <= AV_NOP_C; }
static uint8_t complement(uint8_t op) { return (uint8_t)((op + 1u) % 3u); }
static uint16_t mutation_divisor(uint8_t level)
{
    static const uint16_t rates[] = {0, 1024, 256, 64};
    return level <= 3 ? rates[level] : 0;
}
static uint16_t indel_divisor(uint8_t level)
{
    static const uint16_t rates[] = {0, 4096, 1024, 256};
    return level <= 3 ? rates[level] : 0;
}
static void emit_event(av_world_t *w, uint8_t kind, uint8_t site,
                       const av_organism_t *o)
{
    av_event_t *e = &w->events[w->event_head];
    e->kind = kind; e->site = site; e->length = o->length;
    e->organism_id = o->id; e->parent_id = o->parent_id;
    e->lineage = o->lineage; e->at_update = w->updates;
    w->event_head = (uint8_t)((w->event_head + 1u) % AV_EVENT_CAP);
    if (w->event_count < AV_EVENT_CAP) ++w->event_count;
}

bool av_get_ancestor(uint8_t program[AV_ANCESTOR_LEN])
{
    if (!program) return false;
    memcpy(program, s_ancestor, sizeof(s_ancestor));
    return true;
}

static void reset_cpu(av_organism_t *o)
{
    o->ip = o->read_head = o->write_head = o->flow_head = 0;
    o->allocated = o->copied = o->executed_count = 0;
    o->last_nop_count = 0;
    o->active_stack = 0;
    o->sp[0] = o->sp[1] = 0;
    o->bonus = 0;
    memset(o->daughter, 0, sizeof(o->daughter));
    memset(o->executed_bits, 0, sizeof(o->executed_bits));
    memset(o->last_nops, 0, sizeof(o->last_nops));
    memset(o->reg, 0, sizeof(o->reg));
    memset(o->stack, 0, sizeof(o->stack));
}

void av_reset(av_world_t *w, uint32_t seed)
{
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->seed = seed ? seed : 0xA17E2026u;
    w->rng = w->seed;
    w->mutation_level = 2;
    w->next_id = w->next_lineage = 1;
    // Avida's published 15-instruction HEADS ancestor, placed at 16 sites.
    for (unsigned i = 0; i < 16u; ++i) {
        unsigned loc = random_u32(w) % AV_SITES;
        while (w->organisms[loc].length) loc = (loc + 1u) % AV_SITES;
        av_organism_t *o = &w->organisms[loc];
        o->length = AV_ANCESTOR_LEN;
        memcpy(o->code, s_ancestor, AV_ANCESTOR_LEN);
        o->id = w->next_id++;
        o->lineage = 1;
    }
    w->next_lineage = 2;
}

uint32_t av_genome_hash(const av_organism_t *o)
{
    if (!o || !o->length) return 0;
    uint32_t h = 2166136261u;
    h = (h ^ o->length) * 16777619u;
    for (unsigned i = 0; i < o->length; ++i)
        h = (h ^ o->code[i]) * 16777619u;
    return h;
}

static unsigned suffix_template_len(const av_organism_t *o, unsigned from)
{
    unsigned n = 0;
    while (from + n < o->length && n < 8u &&
           is_nop(o->code[from+n])) ++n;
    return n;
}

static void do_h_search(av_organism_t *o, uint8_t *next)
{
    unsigned from = (unsigned)o->ip + 1u;
    unsigned n = suffix_template_len(o, from);
    *next = (uint8_t)((from + n) % o->length);
    // No template: set flow-head at instruction after h-search.
    o->flow_head = *next;
    if (n == 0) {
        o->reg[1] = o->reg[2] = 0;
        return;
    }
    for (unsigned start = from + n; start + n <= o->length; ++start) {
        unsigned j = 0;
        for (; j < n; ++j)
            if (o->code[start+j] != complement(o->code[from+j])) break;
        if (j == n) {
            o->flow_head = (uint8_t)(start + n);
            o->reg[1] = (int32_t)(start + n - from);
            o->reg[2] = (int32_t)n;
            return;
        }
    }
}

static void remember_copy(av_organism_t *o, uint8_t op)
{
    if (!is_nop(op)) { o->last_nop_count = 0; return; }
    if (o->last_nop_count == 8u) {
        memmove(&o->last_nops[0], &o->last_nops[1], 7u);
        o->last_nop_count = 7u;
    }
    o->last_nops[o->last_nop_count++] = op;
}

static bool label_was_copied(const av_organism_t *o, unsigned from, unsigned n)
{
    if (!n || o->last_nop_count < n) return false;
    unsigned start = o->last_nop_count - n;
    for (unsigned j = 0; j < n; ++j)
        if (o->last_nops[start+j] != complement(o->code[from+j]))
            return false;
    return true;
}

static void place_offspring(av_world_t *w, unsigned src, const uint8_t *code,
                            uint8_t length, bool mutated)
{
    static const int8_t dx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static const int8_t dy[] = {-1,-1,-1,  0, 0,  1, 1, 1};
    unsigned x = src % AV_SIDE, y = src / AV_SIDE;
    unsigned first = random_u32(w) % 8u;
    unsigned target = AV_SITES;
    // Prefer a free neighbor, else replace another local organism.
    for (unsigned i = 0; i < 8u; ++i) {
        unsigned k = (first + i) % 8u;
        int nx = (int)x + dx[k], ny = (int)y + dy[k];
        if (nx < 0 || ny < 0 || nx >= (int)AV_SIDE || ny >= (int)AV_SIDE) continue;
        unsigned p = (unsigned)ny * AV_SIDE + (unsigned)nx;
        if (target == AV_SITES) target = p;
        if (w->organisms[p].length == 0) { target = p; break; }
    }
    if (target == AV_SITES) return;
    av_organism_t *parent = &w->organisms[src];
    av_organism_t *kid = &w->organisms[target];
    const uint32_t parent_id = parent->id;
    const uint32_t lineage = mutated ? w->next_lineage++ : parent->lineage;
    bool replacement = kid->length != 0;
    if (replacement) ++w->replacements;

    memset(kid, 0, sizeof(*kid));
    memcpy(kid->code, code, length);
    kid->length = length;
    kid->id = w->next_id++;
    kid->parent_id = parent_id;
    kid->lineage = lineage;
    kid->born_at = w->updates;
    ++parent->offspring;
    ++w->births;
    emit_event(w, replacement ? AV_EVENT_REPLACE : AV_EVENT_BIRTH,
               (uint8_t)target, kid);
    if (mutated) emit_event(w, AV_EVENT_MUTATION, (uint8_t)target, kid);
}

static void divide(av_world_t *w, unsigned site)
{
    av_organism_t *o = &w->organisms[site];
    unsigned daughter_length = o->write_head >= o->length
                             ? o->write_head - o->length : 0u;
    // Avida validity gate: allocated, enough genome copied/executed, bounds.
    if (!o->allocated || o->length < AV_CODE_MIN ||
        daughter_length < AV_CODE_MIN || daughter_length > AV_CODE_MAX ||
        o->copied < (o->length + 1u) / 2u ||
        o->executed_count < (o->length + 1u) / 2u) {
        ++w->failed_divides;
        return;
    }
    uint8_t sequence[AV_CODE_MAX];
    memcpy(sequence, o->daughter, daughter_length);
    bool changed = memcmp(sequence, o->code,
                          daughter_length < o->length ? daughter_length : o->length) != 0
                   || daughter_length != o->length;
    uint16_t denominator = indel_divisor(w->mutation_level);
    if (denominator && random_u32(w) % denominator == 0) {
        unsigned point = random_u32(w) % daughter_length;
        if ((random_u32(w) & 1u) && daughter_length < AV_CODE_MAX) {
            memmove(sequence+point+1, sequence+point, daughter_length-point);
            sequence[point] = (uint8_t)(random_u32(w) % AV_OPCODE_COUNT);
            ++daughter_length;
            changed = true;
            ++w->insertion_deletions;
        } else if (daughter_length > AV_CODE_MIN) {
            memmove(sequence+point, sequence+point+1, daughter_length-point-1);
            --daughter_length;
            changed = true;
            ++w->insertion_deletions;
        }
    }
    // Official Avida resets the parent CPU after successful division.
    place_offspring(w, site, sequence, (uint8_t)daughter_length, changed);
    reset_cpu(o);
}

static uint8_t modifier_register(const av_organism_t *o, uint8_t current,
                                 uint8_t *next)
{
    if ((unsigned)current + 1u < o->length && is_nop(o->code[current+1u])) {
        *next = (uint8_t)((current+2u) % o->length);
        return o->code[current+1u];
    }
    return 1u; // BX by default
}

static void execute(av_world_t *w, unsigned site)
{
    av_organism_t *o = &w->organisms[site];
    if (!o->length) return;
    uint8_t ip = o->ip % o->length;
    uint8_t op = o->code[ip];
    if (op >= AV_OPCODE_COUNT) op = AV_NOP_A;
    uint8_t next = (uint8_t)((ip + 1u) % o->length);
    uint8_t bit = (uint8_t)(1u << (ip & 7u));
    if (!(o->executed_bits[ip >> 3] & bit)) {
        o->executed_bits[ip >> 3] |= bit;
        ++o->executed_count;
    }
    ++o->instructions;
    uint8_t reg = 1;
    // NOP modifiers for arithmetic/register operations match HEADS convention.
    switch (op) {
        case AV_IF_N_EQU:
        case AV_IF_LESS: {
            reg = modifier_register(o, ip, &next);
            uint8_t other = (uint8_t)((reg + 1u) % 3u);
            bool condition = op == AV_IF_N_EQU ?
                o->reg[reg] != o->reg[other] : o->reg[reg] < o->reg[other];
            if (!condition) next = (uint8_t)((next+1u) % o->length);
            break;
        }
        case AV_SWAP:
            reg = modifier_register(o, ip, &next);
            { uint8_t other = (uint8_t)((reg+1u)%3u);
              int32_t tmp=o->reg[reg];o->reg[reg]=o->reg[other];o->reg[other]=tmp; }
            break;
        case AV_PUSH:
            reg = modifier_register(o, ip, &next);
            if (o->sp[o->active_stack] < 4u)
                o->stack[o->active_stack][o->sp[o->active_stack]++] = o->reg[reg];
            break;
        case AV_POP:
            reg = modifier_register(o, ip, &next);
            if (o->sp[o->active_stack])
                o->reg[reg] = o->stack[o->active_stack][--o->sp[o->active_stack]];
            break;
        case AV_SWAP_STK: o->active_stack ^= 1u; break;
        case AV_SHIFT_R:
            reg = modifier_register(o, ip, &next);
            o->reg[reg] = (int32_t)((uint32_t)o->reg[reg] >> 1u);
            break;
        case AV_SHIFT_L:
            reg = modifier_register(o, ip, &next);
            o->reg[reg] = (int32_t)((uint32_t)o->reg[reg] << 1u);
            break;
        case AV_INC:
            reg = modifier_register(o, ip, &next);
            o->reg[reg] = (int32_t)((uint32_t)o->reg[reg] + 1u);
            break;
        case AV_DEC:
            reg = modifier_register(o, ip, &next);
            o->reg[reg] = (int32_t)((uint32_t)o->reg[reg] - 1u);
            break;
        case AV_ADD:
            o->reg[1] = (int32_t)((uint32_t)o->reg[1] + (uint32_t)o->reg[2]);
            break;
        case AV_SUB:
            o->reg[1] = (int32_t)((uint32_t)o->reg[1] - (uint32_t)o->reg[2]);
            break;
        case AV_NAND:
            o->reg[1] = (int32_t)(~((uint32_t)o->reg[1] & (uint32_t)o->reg[2]));
            break;
        case AV_IO:
            // Environment I/O can be extended with rewarded tasks.
            o->reg[1] = (int32_t)random_u32(w);
            break;
        case AV_H_ALLOC:
            if (!o->allocated) {
                o->allocated = 1;
                o->copied = 0;
                memset(o->daughter, AV_NOP_A, sizeof(o->daughter));
                o->last_nop_count = 0;
            }
            break;
        case AV_H_SEARCH:
            do_h_search(o, &next);
            break;
        case AV_MOV_HEAD:
            if ((unsigned)ip + 1u < o->length && is_nop(o->code[ip+1u])) {
                uint8_t head = o->code[ip+1u];
                next = (uint8_t)((ip+2u) % o->length);
                if (head == AV_NOP_B) o->read_head = o->flow_head;
                else if (head == AV_NOP_C) o->write_head = o->flow_head;
                else next = (uint8_t)(o->flow_head % o->length);
            } else {
                next = (uint8_t)(o->flow_head % o->length);
            }
            break;
        case AV_H_COPY:
            if (o->allocated && o->read_head < o->length &&
                o->write_head >= o->length &&
                o->write_head - o->length < o->length &&
                o->write_head - o->length < AV_CODE_MAX) {
                uint8_t instruction = o->code[o->read_head];
                uint8_t written = instruction;
                ++w->copy_attempts;
                uint16_t denominator = mutation_divisor(w->mutation_level);
                if (denominator && random_u32(w) % denominator == 0) {
                    written = (uint8_t)(random_u32(w) % AV_OPCODE_COUNT);
                    ++w->mutations;
                }
                o->daughter[o->write_head - o->length] = written;
                ++o->read_head;
                ++o->write_head;
                ++o->copied;
                remember_copy(o, instruction);
            }
            break;
        case AV_IF_LABEL: {
            unsigned from = (unsigned)ip + 1u;
            unsigned n = suffix_template_len(o, from);
            bool match = label_was_copied(o, from, n);
            next = (uint8_t)((from+n) % o->length);
            if (!match) next = (uint8_t)((next+1u)%o->length);
            break;
        }
        case AV_H_DIVIDE:
            divide(w, site);
            if (w->organisms[site].ip == 0 && !w->organisms[site].allocated) {
                // A successful divide reset the CPU; begin the ancestor again.
                next = 0;
            }
            break;
        case AV_JMP_HEAD:
            next = (uint8_t)(((int64_t)ip + o->reg[2] +
                              (int64_t)o->length * (1LL << 20)) % o->length);
            break;
        case AV_GET_HEAD:
            o->reg[2] = ip;
            break;
        case AV_SET_FLOW:
            o->flow_head = (uint8_t)(((uint32_t)o->reg[2]) % o->length);
            break;
        case AV_NOP_A:
        case AV_NOP_B:
        case AV_NOP_C:
        default:
            break;
    }
    o->ip = next;
}

void av_step(av_world_t *w, uint32_t budget)
{
    if (!w || w->paused) return;
    for (uint32_t n=0; n<budget; ++n) {
        unsigned search=0;
        while (search < AV_SITES && !w->organisms[w->cursor].length) {
            w->cursor = (uint16_t)((w->cursor+1u)%AV_SITES);
            ++search;
        }
        if (search == AV_SITES) return;
        unsigned current = w->cursor;
        w->cursor = (uint16_t)((w->cursor+1u)%AV_SITES);
        execute(w, current);
        ++w->instructions;
        w->updates = w->instructions / AV_SITES;
    }
}

void av_stats(const av_world_t *w, av_stats_t *s)
{
    if (!w || !s) return;
    memset(s, 0, sizeof(*s));
    for (unsigned i=0; i<AV_SITES; ++i) {
        const av_organism_t *o = &w->organisms[i];
        if (!o->length) continue;
        ++s->population;
        if (o->lineage != 1u) ++s->mutants_alive;
        unsigned matches=0;
        bool already_seen=false;
        for (unsigned j=0; j<AV_SITES; ++j) {
            const av_organism_t *p = &w->organisms[j];
            if (p->length==o->length && p->length &&
                memcmp(p->code,o->code,o->length)==0) {
                ++matches;
                if (j<i) already_seen=true;
            }
        }
        if (!already_seen) ++s->genotype_count;
        if (matches > s->dominant_count) {
            s->dominant_count = (uint16_t)matches;
            s->dominant_hash = av_genome_hash(o);
            s->genotype_length = o->length;
        }
    }
    s->last_birth = w->births;
    s->last_mutation = w->mutations;
}

void av_instruction_name(uint8_t code, char *out, size_t size)
{
    static const char *const names[AV_OPCODE_COUNT] = {
        "nop-A","nop-B","nop-C","if-n-equ","if-less","swap",
        "pop","push","swap-stk","shift-r","shift-l","inc",
        "dec","add","sub","nand","IO","h-alloc","h-divide",
        "h-copy","h-search","mov-head","jmp-head","get-head",
        "if-label","set-flow"
    };
    if (!out || size==0) return;
    (void)snprintf(out,size,"%s",code < AV_OPCODE_COUNT? names[code]:"invalid");
}
const char *av_event_name(uint8_t kind)
{
    switch(kind) {
        case AV_EVENT_BIRTH: return "复制出生";
        case AV_EVENT_MUTATION: return "突变分支";
        case AV_EVENT_REPLACE: return "竞争替换";
        case AV_EVENT_FAIL: return "分裂失败";
        default: return "等待";
    }
}

static uint32_t fnv_bytes(const void *data,size_t len)
{
    const uint8_t *p=data;uint32_t h=2166136261u;
    for(size_t i=0;i<len;++i)h=(h^p[i])*16777619u;
    return h;
}
void av_snapshot_encode(const av_world_t *w,uint32_t seq,av_snapshot_t *s)
{
    if(!w||!s)return;
    memset(s,0,sizeof(*s));
    s->magic=AV_SAVE_MAGIC;s->version=AV_SAVE_VERSION;s->sequence=seq;
    s->world=*w;
    s->checksum=fnv_bytes(s,offsetof(av_snapshot_t,checksum));
}
bool av_snapshot_decode(av_world_t *w,const av_snapshot_t *s)
{
    if(!w||!s||s->magic!=AV_SAVE_MAGIC||s->version!=AV_SAVE_VERSION||
       s->checksum!=fnv_bytes(s,offsetof(av_snapshot_t,checksum)))return false;
    const av_world_t *src=&s->world;
    if(!src->rng||src->mutation_level>3u||src->paused>1u||
       src->cursor>=AV_SITES||src->event_count>AV_EVENT_CAP||
       src->event_head>=AV_EVENT_CAP)return false;
    for(unsigned i=0;i<AV_SITES;++i){
        const av_organism_t *o=&src->organisms[i];
        if(o->length>AV_CODE_MAX|| (o->length&&o->length<AV_CODE_MIN)||
           o->active_stack>1u||o->sp[0]>4u||o->sp[1]>4u||
           o->last_nop_count>8u||o->copied>AV_CODE_MAX||
           o->read_head>AV_CODE_MAX||o->write_head>AV_CODE_MAX*2u||
           o->flow_head>AV_CODE_MAX*2u||
           (o->length&&o->ip>=o->length))return false;
        for(unsigned j=0;j<o->length;++j)
            if(o->code[j]>=AV_OPCODE_COUNT)return false;
    }
    *w=*src;
    return true;
}
