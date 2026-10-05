#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define NS_AP_MAX 40
#define NS_BLE_MAX 32
#define NS_CAPTURE_MAX 64
#define NS_SNAPLEN 36
#define NS_SERVICE_MAX 24
#define NS_HISTORY_MAX 12
#define NS_SESSION_MAX 8

typedef struct {
    uint8_t type, subtype, header_len, eapol_message;
    uint16_t reason;
    bool protected_frame, retry, has_pair;
    uint8_t bssid[6], station[6];
    uint64_t replay;
} ns_frame_t;
bool ns_parse_frame(const uint8_t *p, size_t n, ns_frame_t *out);
/* Valid UTF-8 in the supported ASCII/CJK inventory; unknown text becomes '?'. */
void ns_text(char *out, size_t cap, const uint8_t *p, size_t n);
void ns_ble_name(const uint8_t *p, size_t n, char *out, size_t cap);
typedef struct {
    bool has_flags, has_company, has_uuid, has_tx;
    uint8_t flags;
    uint16_t company, uuid;
    int8_t tx;
} ns_ble_meta_t;
void ns_ble_meta(const uint8_t *p, size_t n, ns_ble_meta_t *out);
void ns_channel_scores(const uint8_t *channels, const int8_t *rssis, size_t n,
                       uint16_t count[14], uint32_t score[14]);
/* Decode compressed DNS names with bounded jumps. offset advances on success. */
bool ns_dns_name(const uint8_t *p, size_t n, size_t *offset, char *out, size_t cap);
size_t ns_dns_query(uint8_t *p, size_t cap, const char *name);
size_t ns_pcap_header(uint8_t out[24]);
size_t ns_pcap_record(uint8_t *out, uint64_t us, const uint8_t *p,
                      uint32_t saved, uint32_t original);

typedef struct { uint64_t start; uint32_t count; bool alarm; } ns_burst_t;
bool ns_burst_add(ns_burst_t *b, uint64_t now, uint16_t threshold);
void ns_burst_tick(ns_burst_t *b, uint64_t now);
typedef struct {
    bool used;
    uint8_t bssid[6], station[6], seen;
    uint64_t replay, last_us;
    uint32_t repeats;
} ns_session_t;
/* Presence means observed key-info roles, never proves authentication success. */
void ns_session_observe(ns_session_t sessions[NS_SESSION_MAX], const ns_frame_t *f,
                        uint64_t now);
