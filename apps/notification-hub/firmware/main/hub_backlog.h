#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Store identifiers, never hundreds of notification bodies, in internal RAM. */
#define HUB_BACKLOG_CAPACITY 512
typedef struct {
    uint32_t uids[HUB_BACKLOG_CAPACITY];
    uint16_t head, count;
} hub_backlog_t;
bool hub_backlog_push(hub_backlog_t *queue, uint32_t uid);
bool hub_backlog_pop(hub_backlog_t *queue, uint32_t *uid);
void hub_backlog_remove(hub_backlog_t *queue, uint32_t uid);
