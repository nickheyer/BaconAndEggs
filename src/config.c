#include "config.h"
#include "util.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#define FLASH_CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

config_t config;
server_monitor_t monitors[MAX_SERVERS];

void config_save(void) {
    config.magic = FLASH_MAGIC;

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CONFIG_OFFSET, FLASH_SECTOR_SIZE);

    uint8_t buf[FLASH_SECTOR_SIZE] __attribute__((aligned(256)));
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &config, sizeof(config_t));
    flash_range_program(FLASH_CONFIG_OFFSET, buf, FLASH_SECTOR_SIZE);

    restore_interrupts(ints);
    printf("Config saved to flash\n");
}

bool config_load(void) {
    const uint8_t *flash = (const uint8_t *)(XIP_BASE + FLASH_CONFIG_OFFSET);
    const uint32_t *magic = (const uint32_t *)flash;

    if (*magic != FLASH_MAGIC) {
        return false;
    }

    memcpy(&config, flash, sizeof(config_t));
    printf("Config loaded from flash (%d servers, autowake=%s)\n",
           config.server_count, config.autowake ? "on" : "off");
    return true;
}

#ifndef DEFAULT_AUTOWAKE
#define DEFAULT_AUTOWAKE 0
#endif

#ifndef DEFAULT_SERVERS
#define DEFAULT_SERVERS ""
#endif

#ifndef DEFAULT_NTP_SERVER
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#endif

#ifndef DEFAULT_UTC_OFFSET
#define DEFAULT_UTC_OFFSET 0
#endif

#ifndef DEFAULT_MQTT_HOST
#define DEFAULT_MQTT_HOST ""
#endif

#ifndef DEFAULT_MQTT_PORT
#define DEFAULT_MQTT_PORT 1883
#endif

#ifndef DEFAULT_MQTT_USER
#define DEFAULT_MQTT_USER ""
#endif

#ifndef DEFAULT_MQTT_PASS
#define DEFAULT_MQTT_PASS ""
#endif

#ifndef DEFAULT_WEBHOOK_URL
#define DEFAULT_WEBHOOK_URL ""
#endif

void config_load_defaults(void) {
    memset(&config, 0, sizeof(config));
    config.magic = FLASH_MAGIC;
    config.autowake = DEFAULT_AUTOWAKE;
    config.server_count = 0;
    config.schedule_count = 0;
    config.utc_offset_minutes = DEFAULT_UTC_OFFSET;

    // NTP
    strncpy(config.ntp_server, DEFAULT_NTP_SERVER, HOST_LEN - 1);

    // MQTT defaults
    if (strlen(DEFAULT_MQTT_HOST) > 0) {
        config.mqtt.enabled = true;
        strncpy(config.mqtt.host, DEFAULT_MQTT_HOST, HOST_LEN - 1);
        config.mqtt.port = DEFAULT_MQTT_PORT;
        strncpy(config.mqtt.user, DEFAULT_MQTT_USER, CRED_LEN - 1);
        strncpy(config.mqtt.pass, DEFAULT_MQTT_PASS, CRED_LEN - 1);
    } else {
        config.mqtt.enabled = false;
        config.mqtt.port = 1883;
    }

    // Webhook defaults
    if (strlen(DEFAULT_WEBHOOK_URL) > 0) {
        config.webhook.enabled = true;
        strncpy(config.webhook.url, DEFAULT_WEBHOOK_URL, URL_LEN - 1);
    } else {
        config.webhook.enabled = false;
    }

    // Parse DEFAULT_SERVERS
    char buf[512];
    strncpy(buf, DEFAULT_SERVERS, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "|");
    while (tok && config.server_count < MAX_SERVERS) {
        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            const char *name = tok;
            char *mac_str = eq + 1;

            char *comma = strchr(mac_str, ',');
            const char *ip_str = NULL;
            if (comma) {
                *comma = '\0';
                ip_str = comma + 1;
            }

            server_entry_t *s = &config.servers[config.server_count];
            strncpy(s->name, name, NAME_LEN - 1);
            if (parse_mac(mac_str, s->mac)) {
                s->active = true;
                s->ip_addr = 0;
                if (ip_str && *ip_str) {
                    if (!parse_ip(ip_str, &s->ip_addr)) {
                        printf("Bad IP for default '%s': %s\n", name, ip_str);
                    }
                }
                config.server_count++;
                printf("Default server: %s (%s)\n", name, mac_str);
            } else {
                printf("Bad MAC for default '%s': %s\n", name, mac_str);
            }
        }
        tok = strtok(NULL, "|");
    }
}
