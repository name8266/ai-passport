#pragma once
/* Shared, exact-width FreeRTOS archive command envelope.
 * Every xQueueSend sender must supply this full object: queues copy
 * sizeof(archive_control_event_t), not the size of the sender's pointer. */
#include <stdint.h>
typedef struct {
    uint8_t kind;
    uint32_t fingerprint;
    uint32_t revision;
    uint32_t id;
} archive_control_event_t;
static inline archive_control_event_t hub_control_make(uint8_t kind) {
    archive_control_event_t command={0};
    command.kind=kind;
    return command;
}
