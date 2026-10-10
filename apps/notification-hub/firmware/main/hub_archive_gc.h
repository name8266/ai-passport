#pragma once
/* Keep FAT journal compaction out of the per-three-notice hot path.
 * 64 journal snapshots ~= 24 KiB of deferred source data. */
#include <stdint.h>
#include <stdbool.h>
#define HUB_ARCHIVE_RECLAIM_THRESHOLD 64u
static inline bool hub_archive_gc_due(uint32_t committed_rows,bool full) {
    return committed_rows>=HUB_ARCHIVE_RECLAIM_THRESHOLD ||
           (full && committed_rows>0u);
}
