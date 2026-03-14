#include "mqtt_client.h"
#include "config.h"
#include "util.h"
#include "wol.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"

static mqtt_client_t *mqtt_client = NULL;
static bool connected = false;
static uint32_t last_publish_ms = 0;
static uint32_t last_reconnect_ms = 0;
static uint32_t reconnect_delay_ms = 5000;
static ip_addr_t mqtt_server_ip;
static bool dns_resolved = false;
static bool dns_pending = false;

#define MQTT_STATUS_INTERVAL_MS  60000
#define MQTT_RECONNECT_MAX_MS    60000

// Forward declarations
static void mqtt_connect(void);

static void mqtt_connection_cb(mqtt_client_t *client, void *arg,
                                mqtt_connection_status_t status) {
    (void)arg;
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("MQTT: connected\n");
        connected = true;
        reconnect_delay_ms = 5000;

        // Subscribe to command topics
        mqtt_subscribe(client, "baconandeggs/cmd/wake", 0, NULL, NULL);
        mqtt_subscribe(client, "baconandeggs/cmd/wake-all", 0, NULL, NULL);
    } else {
        printf("MQTT: connection failed (status %d)\n", status);
        connected = false;
    }
}

static void mqtt_incoming_publish_cb(void *arg, const char *topic,
                                      u32_t tot_len) {
    (void)arg;
    (void)tot_len;
    printf("MQTT: incoming on '%s'\n", topic);
}

static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len,
                                   u8_t flags) {
    (void)arg;
    (void)flags;

    // Simple payload extraction — server name
    char payload[NAME_LEN];
    uint16_t copy_len = len < NAME_LEN - 1 ? len : NAME_LEN - 1;
    memcpy(payload, data, copy_len);
    payload[copy_len] = '\0';

    // Trim whitespace
    int plen = strlen(payload);
    while (plen > 0 && (payload[plen-1] == '\n' || payload[plen-1] == '\r' || payload[plen-1] == ' '))
        payload[--plen] = '\0';

    if (plen == 0) {
        // wake-all topic with empty or any payload
        printf("MQTT: wake all\n");
        wake_all();
        return;
    }

    // Try to find server by name
    for (int i = 0; i < config.server_count; i++) {
        if (config.servers[i].active && strcmp(config.servers[i].name, payload) == 0) {
            printf("MQTT: waking '%s'\n", payload);
            wake_server_monitored(i);
            return;
        }
    }

    printf("MQTT: unknown server '%s'\n", payload);
}

static void mqtt_connect(void) {
    if (!mqtt_client) {
        mqtt_client = mqtt_client_new();
        if (!mqtt_client) {
            printf("MQTT: failed to create client\n");
            return;
        }
        mqtt_set_inpub_callback(mqtt_client, mqtt_incoming_publish_cb,
                                mqtt_incoming_data_cb, NULL);
    }

    struct mqtt_connect_client_info_t ci;
    memset(&ci, 0, sizeof(ci));
    ci.client_id = "baconandeggs";
    ci.client_user = strlen(config.mqtt.user) > 0 ? config.mqtt.user : NULL;
    ci.client_pass = strlen(config.mqtt.pass) > 0 ? config.mqtt.pass : NULL;
    ci.keep_alive = 60;
    ci.will_topic = "baconandeggs/status";
    ci.will_msg = "{\"online\":false}";
    ci.will_qos = 0;
    ci.will_retain = 1;

    cyw43_arch_lwip_begin();
    err_t err = mqtt_client_connect(mqtt_client, &mqtt_server_ip,
                                     config.mqtt.port, mqtt_connection_cb,
                                     NULL, &ci);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("MQTT: connect call failed (%d)\n", err);
    }
}

static void dns_callback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name;
    (void)arg;
    dns_pending = false;

    if (ipaddr) {
        mqtt_server_ip = *ipaddr;
        dns_resolved = true;
        printf("MQTT: resolved %s -> %s\n", config.mqtt.host, ipaddr_ntoa(ipaddr));
        mqtt_connect();
    } else {
        printf("MQTT: DNS resolution failed for %s\n", config.mqtt.host);
    }
}

void bae_mqtt_init(void) {
    if (!config.mqtt.enabled || strlen(config.mqtt.host) == 0) {
        printf("MQTT: disabled\n");
        return;
    }

    connected = false;
    dns_resolved = false;
    dns_pending = false;
    reconnect_delay_ms = 5000;

    printf("MQTT: resolving %s...\n", config.mqtt.host);

    // Try to parse as IP first
    if (ipaddr_aton(config.mqtt.host, &mqtt_server_ip)) {
        dns_resolved = true;
        mqtt_connect();
        return;
    }

    // DNS resolve
    dns_pending = true;
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(config.mqtt.host, &mqtt_server_ip, dns_callback, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) {
        // Already cached
        dns_pending = false;
        dns_resolved = true;
        mqtt_connect();
    } else if (err != ERR_INPROGRESS) {
        dns_pending = false;
        printf("MQTT: DNS error\n");
    }
}

bool bae_mqtt_connected(void) {
    return connected;
}

void bae_mqtt_publish_state(const char *server_name, const char *state) {
    if (!connected || !mqtt_client)
        return;

    char topic[64];
    snprintf(topic, sizeof(topic), "baconandeggs/server/%s/state", server_name);

    cyw43_arch_lwip_begin();
    mqtt_publish(mqtt_client, topic, state, strlen(state), 0, 1, NULL, NULL);
    cyw43_arch_lwip_end();
}

static void mqtt_publish_status(void) {
    if (!connected || !mqtt_client)
        return;

    char buf[512];
    int pos = snprintf(buf, sizeof(buf),
                       "{\"online\":true,\"servers\":%d,\"autowake\":%s,\"servers_list\":[",
                       config.server_count, config.autowake ? "true" : "false");

    for (int i = 0; i < config.server_count && pos < (int)sizeof(buf) - 64; i++) {
        if (!config.servers[i].active) continue;
        const char *state = monitors[i].wol_active ? "waking" :
                            monitors[i].up ? "up" : "down";
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "{\"name\":\"%s\",\"state\":\"%s\"}",
                        config.servers[i].name, state);
    }
    snprintf(buf + pos, sizeof(buf) - pos, "]}");

    cyw43_arch_lwip_begin();
    mqtt_publish(mqtt_client, "baconandeggs/status", buf, strlen(buf), 0, 1, NULL, NULL);
    cyw43_arch_lwip_end();
}

void bae_mqtt_tick(uint32_t now_ms) {
    if (!config.mqtt.enabled)
        return;

    // Reconnect logic
    if (!connected && dns_resolved && !dns_pending) {
        if ((now_ms - last_reconnect_ms) >= reconnect_delay_ms) {
            last_reconnect_ms = now_ms;
            printf("MQTT: reconnecting...\n");
            mqtt_connect();

            // Exponential backoff
            reconnect_delay_ms *= 2;
            if (reconnect_delay_ms > MQTT_RECONNECT_MAX_MS)
                reconnect_delay_ms = MQTT_RECONNECT_MAX_MS;
        }
    }

    // Periodic status publish
    if (connected && (now_ms - last_publish_ms) >= MQTT_STATUS_INTERVAL_MS) {
        last_publish_ms = now_ms;
        mqtt_publish_status();
    }
}
