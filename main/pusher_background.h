#pragma once
#include <stdint.h>
#define CP_BG_MAGIC 0x31474243u
/* Exact affine depth runs, little endian. Offsets count records, per scanline. */
typedef struct {uint32_t magic,width,height,offset[321];} cp_bg_header_t;
typedef struct {uint16_t length,color;uint32_t start;int32_t step;} cp_bg_run_t;
_Static_assert(sizeof(cp_bg_run_t)==12,"background record layout");
