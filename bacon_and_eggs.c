/**
 * BaconAndEggs — Wake-on-LAN Server for Pico W
 *
 * TCP on port 4242.
 * Config persists to flash. Defaults come from build-time defines.
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#define TCP_PORT 4242
#define WOL_PORT 9
#define BUF_SIZE 512
#define MAX_SERVERS 32
#define NAME_LEN 32

#define FLASH_CONFIG_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define FLASH_MAGIC 0x574F4C43  // "WOLC"

typedef struct {
    char name[NAME_LEN];
    uint8_t mac[6];
    bool active;
} server_entry_t;

typedef struct {
    uint32_t magic;
    bool autowake;
    uint8_t server_count;
    server_entry_t servers[MAX_SERVERS];
} config_t;

static config_t config;

static bool parse_mac(const char *str, uint8_t mac[6]) {
    unsigned int vals[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &vals[0], &vals[1], &vals[2],
               &vals[3], &vals[4], &vals[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (vals[i] > 255) return false;
        mac[i] = (uint8_t)vals[i];
    }
    return true;
}

static void mac_to_str(const uint8_t mac[6], char *out) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void config_save(void) {
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

static bool config_load(void) {
    const config_t *stored = (const config_t *)(XIP_BASE + FLASH_CONFIG_OFFSET);

    if (stored->magic != FLASH_MAGIC) {
        return false;
    }

    memcpy(&config, stored, sizeof(config_t));
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

static void config_load_defaults(void) {
    memset(&config, 0, sizeof(config));
    config.magic = FLASH_MAGIC;
    config.autowake = DEFAULT_AUTOWAKE;
    config.server_count = 0;

    char buf[512];
    strncpy(buf, DEFAULT_SERVERS, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *entry = strtok(buf, ";");
    while (entry && config.server_count < MAX_SERVERS) {
        char *eq = strchr(entry, '=');
        if (eq) {
            *eq = '\0';
            const char *name = entry;
            const char *mac_str = eq + 1;

            server_entry_t *s = &config.servers[config.server_count];
            strncpy(s->name, name, NAME_LEN - 1);
            if (parse_mac(mac_str, s->mac)) {
                s->active = true;
                config.server_count++;
                printf("Default server: %s (%s)\n", name, mac_str);
            } else {
                printf("Bad MAC for default '%s': %s\n", name, mac_str);
            }
        }
        entry = strtok(NULL, ";");
    }
}

static bool send_wol_packet(const uint8_t mac[6]) {
    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(&packet[6 + i * 6], mac, 6);
    }

    struct udp_pcb *pcb = udp_new();
    if (!pcb) {
        printf("Failed to create UDP PCB\n");
        return false;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(packet), PBUF_RAM);
    if (!p) {
        printf("Failed to alloc pbuf\n");
        udp_remove(pcb);
        return false;
    }

    memcpy(p->payload, packet, sizeof(packet));

    ip_addr_t broadcast;
    IP4_ADDR(&broadcast, 255, 255, 255, 255);

    err_t err = udp_sendto(pcb, p, &broadcast, WOL_PORT);
    pbuf_free(p);
    udp_remove(pcb);

    return err == ERR_OK;
}

static void wake_server(const server_entry_t *s) {
    char mac_str[18];
    mac_to_str(s->mac, mac_str);
    printf("Waking '%s' (%s)...\n", s->name, mac_str);

    if (send_wol_packet(s->mac)) {
        printf("  WOL sent OK\n");
    } else {
        printf("  WOL send FAILED\n");
    }
}

static void wake_all(void) {
    printf("Waking all servers...\n");
    for (int i = 0; i < config.server_count; i++) {
        if (config.servers[i].active) {
            wake_server(&config.servers[i]);
            sleep_ms(100);
        }
    }
}

typedef struct {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
} cmd_server_t;

static void send_response(struct tcp_pcb *tpcb, const char *msg) {
    tcp_write(tpcb, msg, strlen(msg), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
}

static void trim(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ')) {
        str[--len] = '\0';
    }
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}

static void handle_command(struct tcp_pcb *tpcb, const char *cmd) {
    char resp[256];

    if (strcmp(cmd, "wake all") == 0) {
        wake_all();
        snprintf(resp, sizeof(resp), "Sent WOL to all %d servers\r\n", config.server_count);
        send_response(tpcb, resp);

    } else if (strncmp(cmd, "wake ", 5) == 0) {
        const char *name = cmd + 5;
        bool found = false;
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(name, config.servers[i].name) == 0) {
                wake_server(&config.servers[i]);
                snprintf(resp, sizeof(resp), "Sent WOL to '%s'\r\n", name);
                send_response(tpcb, resp);
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(resp, sizeof(resp), "Unknown server '%s'. Type 'list' to see servers.\r\n", name);
            send_response(tpcb, resp);
        }

    } else if (strcmp(cmd, "list") == 0) {
        if (config.server_count == 0) {
            send_response(tpcb, "No servers configured. Use 'add <name> <mac>' to add one.\r\n");
        } else {
            send_response(tpcb, "Configured servers:\r\n");
            for (int i = 0; i < config.server_count; i++) {
                if (config.servers[i].active) {
                    char mac_str[18];
                    mac_to_str(config.servers[i].mac, mac_str);
                    snprintf(resp, sizeof(resp), "  %s (%s)\r\n",
                             config.servers[i].name, mac_str);
                    send_response(tpcb, resp);
                }
            }
        }

    } else if (strncmp(cmd, "add ", 4) == 0) {
        char name[NAME_LEN];
        char mac_str[18];
        if (sscanf(cmd + 4, "%31s %17s", name, mac_str) != 2) {
            send_response(tpcb, "Usage: add <name> <mac>\r\nExample: add mypc AA:BB:CC:DD:EE:FF\r\n");
            return;
        }

        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                if (!parse_mac(mac_str, config.servers[i].mac)) {
                    send_response(tpcb, "Invalid MAC address format\r\n");
                    return;
                }
                config_save();
                snprintf(resp, sizeof(resp), "Updated '%s' with MAC %s (saved)\r\n", name, mac_str);
                send_response(tpcb, resp);
                return;
            }
        }

        if (config.server_count >= MAX_SERVERS) {
            send_response(tpcb, "Server list full (max 32)\r\n");
            return;
        }

        server_entry_t *s = &config.servers[config.server_count];
        if (!parse_mac(mac_str, s->mac)) {
            send_response(tpcb, "Invalid MAC address format\r\n");
            return;
        }
        strncpy(s->name, name, NAME_LEN - 1);
        s->active = true;
        config.server_count++;
        config_save();
        snprintf(resp, sizeof(resp), "Added '%s' (%s) (saved)\r\n", name, mac_str);
        send_response(tpcb, resp);

    } else if (strncmp(cmd, "remove ", 7) == 0) {
        const char *name = cmd + 7;
        bool found = false;
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                for (int j = i; j < config.server_count - 1; j++) {
                    config.servers[j] = config.servers[j + 1];
                }
                config.server_count--;
                memset(&config.servers[config.server_count], 0, sizeof(server_entry_t));
                config_save();
                snprintf(resp, sizeof(resp), "Removed '%s' (saved)\r\n", name);
                send_response(tpcb, resp);
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(resp, sizeof(resp), "Server '%s' not found\r\n", name);
            send_response(tpcb, resp);
        }

    } else if (strcmp(cmd, "autowake on") == 0) {
        config.autowake = true;
        config_save();
        send_response(tpcb, "Auto-wake ON (saved)\r\n");

    } else if (strcmp(cmd, "autowake off") == 0) {
        config.autowake = false;
        config_save();
        send_response(tpcb, "Auto-wake OFF (saved)\r\n");

    } else if (strcmp(cmd, "autowake") == 0) {
        snprintf(resp, sizeof(resp), "Auto-wake: %s\r\n", config.autowake ? "ON" : "OFF");
        send_response(tpcb, resp);

    } else if (strcmp(cmd, "save") == 0) {
        config_save();
        send_response(tpcb, "Config saved to flash\r\n");

    } else if (strcmp(cmd, "factory") == 0) {
        config_load_defaults();
        config_save();
        send_response(tpcb, "Reset to factory defaults (saved)\r\n");

    } else if (strcmp(cmd, "help") == 0) {
        send_response(tpcb,
            "Commands:\r\n"
            "  wake all              - wake all servers\r\n"
            "  wake <name>           - wake a specific server\r\n"
            "  list                  - show configured servers\r\n"
            "  add <name> <mac>      - add/update a server\r\n"
            "  remove <name>         - remove a server\r\n"
            "  autowake on|off       - set auto-wake on boot\r\n"
            "  autowake              - show auto-wake setting\r\n"
            "  save                  - save config to flash\r\n"
            "  factory               - reset to defaults\r\n"
            "  help                  - this message\r\n");

    } else {
        send_response(tpcb, "Unknown command. Type 'help' for options.\r\n");
    }
}

static err_t server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    cmd_server_t *state = (cmd_server_t *)arg;

    if (!p) {
        printf("Client disconnected\n");
        tcp_close(tpcb);
        state->client_pcb = NULL;
        return ERR_OK;
    }

    char buf[BUF_SIZE];
    uint16_t len = p->tot_len < BUF_SIZE - 1 ? p->tot_len : BUF_SIZE - 1;
    pbuf_copy_partial(p, buf, len, 0);
    buf[len] = '\0';
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    trim(buf);
    printf("Command: '%s'\n", buf);

    if (strlen(buf) > 0) {
        handle_command(tpcb, buf);
    }

    return ERR_OK;
}

static err_t server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    cmd_server_t *state = (cmd_server_t *)arg;

    if (err != ERR_OK || client_pcb == NULL) {
        return ERR_VAL;
    }

    printf("Client connected\n");
    state->client_pcb = client_pcb;
    tcp_arg(client_pcb, state);
    tcp_recv(client_pcb, server_recv);

    send_response(client_pcb, "BaconAndEggs. Type 'help' for commands.\r\n");
    return ERR_OK;
}

static bool server_open(cmd_server_t *state) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("Failed to create PCB\n");
        return false;
    }

    if (tcp_bind(pcb, NULL, TCP_PORT)) {
        printf("Failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        printf("Failed to listen\n");
        tcp_close(pcb);
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, server_accept);

    printf("TCP command server on %s:%u\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);
    return true;
}

int main() {
    stdio_init_all();
    sleep_ms(5000);

    printf("=== BaconAndEggs ===\n");

    if (cyw43_arch_init()) {
        printf("WiFi init failed\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Connecting to WiFi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("WiFi connect failed\n");
        return 1;
    }
    printf("Connected. IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

    if (!config_load()) {
        printf("No saved config, loading defaults\n");
        config_load_defaults();
        config_save();
    }

    if (config.autowake) {
        printf("--- Auto-wake on boot ---\n");
        wake_all();
    } else {
        printf("Auto-wake disabled, skipping\n");
    }

    cmd_server_t state = {0};
    if (!server_open(&state)) {
        return 1;
    }

    while (true) {
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
#else
        sleep_ms(1000);
#endif
    }

    cyw43_arch_deinit();
    return 0;
}
