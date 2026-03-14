#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DISCOVER_IDLE,
    DISCOVER_PENDING,
    DISCOVER_FOUND,
    DISCOVER_NOT_FOUND,
} discover_state_t;

void discovery_start(uint32_t ip_addr);
void discovery_tick(uint32_t now_ms);
discover_state_t discovery_state(void);
const uint8_t *discovery_result_mac(void);

#endif
