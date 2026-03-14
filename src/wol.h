#ifndef WOL_H
#define WOL_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define PING_ID                 0xBE01
#define PING_DATA_SIZE          32
#define PING_TIMEOUT_MS         3000
#define HEALTH_INTERVAL_MS      300000
#define WOL_PING_INTERVAL_MS    10000
#define WOL_PINGS_PER_ATTEMPT   6
#define WOL_MAX_ATTEMPTS        5
#define WOL_PORT                9

void ping_init(void);
void ping_send(int server_idx);
bool send_wol_packet(const uint8_t mac[6]);
void wake_server(const server_entry_t *s);
void wake_server_monitored(int idx);
void wake_all(void);
void monitor_tick(uint32_t now);

#endif
