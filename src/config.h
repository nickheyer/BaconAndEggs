#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_SERVERS     32
#define MAX_SCHEDULES   16
#define NAME_LEN        32
#define HOST_LEN        64
#define CRED_LEN        32
#define URL_LEN         128

#define FLASH_MAGIC       0x574F4C45

typedef struct {
    char name[NAME_LEN];
    uint8_t mac[6];
    bool active;
    uint8_t _pad;
    uint32_t ip_addr;  // network byte order, 0 = not set
} server_entry_t;      // 44 bytes

typedef struct {
    uint8_t day_mask;    // bit0=Sun..bit6=Sat, 0x7F=daily
    uint8_t hour;        // 0-23
    uint8_t minute;      // 0-59
    int8_t server_idx;   // index or -1=all
} schedule_entry_t;      // 4 bytes

typedef struct {
    bool enabled;
    uint8_t _pad;
    uint16_t port;
    char host[HOST_LEN];
    char user[CRED_LEN];
    char pass[CRED_LEN];
} mqtt_config_t;         // 132 bytes

typedef struct {
    bool enabled;
    uint8_t _pad[3];
    char url[URL_LEN];
} webhook_config_t;      // 132 bytes

typedef struct {
    uint32_t magic;
    bool autowake;
    uint8_t server_count;
    uint8_t schedule_count;
    int8_t _pad0;
    server_entry_t servers[MAX_SERVERS];
    int16_t utc_offset_minutes;
    uint8_t _pad1[2];
    schedule_entry_t schedules[MAX_SCHEDULES];
    mqtt_config_t mqtt;
    webhook_config_t webhook;
    char ntp_server[HOST_LEN];
} config_t;

typedef struct {
    bool up;
    bool wol_active;
    uint8_t wol_count;
    uint8_t ping_count;
    bool ping_pending;
    uint32_t last_ping_ms;
    uint32_t last_health_ms;
    uint16_t ping_seq;
} server_monitor_t;

// Global state
extern config_t config;
extern server_monitor_t monitors[MAX_SERVERS];

void config_save(void);
bool config_load(void);
void config_load_defaults(void);

#endif
