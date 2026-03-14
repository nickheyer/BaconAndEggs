#include "tcp_cmd.h"
#include "config.h"
#include "util.h"
#include "wol.h"
#include "scheduler.h"
#include "mqtt_client.h"
#include "webhook.h"
#include "discovery.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/tcp.h"

typedef struct {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
} cmd_server_t;

static cmd_server_t cmd_state = {0};

static void send_response(struct tcp_pcb *tpcb, const char *msg) {
    tcp_write(tpcb, msg, strlen(msg), TCP_WRITE_FLAG_COPY);
    tcp_output(tpcb);
}

static void handle_command(struct tcp_pcb *tpcb, const char *cmd) {
    char resp[512];

    // --- Wake ---
    if (strcmp(cmd, "wake all") == 0) {
        wake_all();
        snprintf(resp, sizeof(resp), "Sent WOL to all %d servers\r\n", config.server_count);
        send_response(tpcb, resp);

    } else if (strncmp(cmd, "wake ", 5) == 0) {
        const char *name = cmd + 5;
        bool found = false;
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(name, config.servers[i].name) == 0) {
                wake_server_monitored(i);
                if (config.servers[i].ip_addr != 0) {
                    snprintf(resp, sizeof(resp), "Sent WOL to '%s' — verifying with ping\r\n", name);
                } else {
                    snprintf(resp, sizeof(resp), "Sent WOL to '%s'\r\n", name);
                }
                send_response(tpcb, resp);
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(resp, sizeof(resp), "Unknown server '%s'. Type 'list' to see servers.\r\n", name);
            send_response(tpcb, resp);
        }

    // --- List ---
    } else if (strcmp(cmd, "list") == 0) {
        if (config.server_count == 0) {
            send_response(tpcb, "No servers configured. Use 'add <name> <mac>' to add one.\r\n");
        } else {
            send_response(tpcb, "Configured servers:\r\n");
            for (int i = 0; i < config.server_count; i++) {
                if (config.servers[i].active) {
                    char mac_str[18];
                    mac_to_str(config.servers[i].mac, mac_str);

                    if (config.servers[i].ip_addr != 0) {
                        char ip_str[16];
                        ip_to_str(config.servers[i].ip_addr, ip_str);
                        const char *state = monitors[i].wol_active ? "WAKING" :
                                            monitors[i].up ? "UP" : "DOWN";
                        snprintf(resp, sizeof(resp), "  %s (%s) %s [%s]\r\n",
                                 config.servers[i].name, mac_str, ip_str, state);
                    } else {
                        snprintf(resp, sizeof(resp), "  %s (%s)\r\n",
                                 config.servers[i].name, mac_str);
                    }
                    send_response(tpcb, resp);
                }
            }
        }

    // --- Add ---
    } else if (strncmp(cmd, "add ", 4) == 0) {
        char name[NAME_LEN];
        char mac_str[18];
        char ip_str[16];
        int n = sscanf(cmd + 4, "%31s %17s %15s", name, mac_str, ip_str);
        if (n < 2) {
            send_response(tpcb, "Usage: add <name> <mac> [<ip>]\r\nExample: add mypc AA:BB:CC:DD:EE:FF 192.168.1.100\r\n");
            return;
        }

        uint32_t ip_val = 0;
        if (n >= 3) {
            if (!parse_ip(ip_str, &ip_val)) {
                send_response(tpcb, "Invalid IP address format\r\n");
                return;
            }
        }

        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                if (!parse_mac(mac_str, config.servers[i].mac)) {
                    send_response(tpcb, "Invalid MAC address format\r\n");
                    return;
                }
                if (n >= 3) config.servers[i].ip_addr = ip_val;
                config_save();
                snprintf(resp, sizeof(resp), "Updated '%s' (saved)\r\n", name);
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
        s->ip_addr = ip_val;
        memset(&monitors[config.server_count], 0, sizeof(server_monitor_t));
        config.server_count++;
        config_save();
        snprintf(resp, sizeof(resp), "Added '%s' (%s) (saved)\r\n", name, mac_str);
        send_response(tpcb, resp);

    // --- Remove ---
    } else if (strncmp(cmd, "remove ", 7) == 0) {
        const char *name = cmd + 7;
        bool found = false;
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                for (int j = i; j < config.server_count - 1; j++) {
                    config.servers[j] = config.servers[j + 1];
                    monitors[j] = monitors[j + 1];
                }
                config.server_count--;
                memset(&config.servers[config.server_count], 0, sizeof(server_entry_t));
                memset(&monitors[config.server_count], 0, sizeof(server_monitor_t));
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

    // --- Set IP ---
    } else if (strncmp(cmd, "setip ", 6) == 0) {
        char name[NAME_LEN];
        char ip_str[16];
        if (sscanf(cmd + 6, "%31s %15s", name, ip_str) != 2) {
            send_response(tpcb, "Usage: setip <name> <ip>\r\n");
            return;
        }
        uint32_t ip_val;
        if (!parse_ip(ip_str, &ip_val)) {
            send_response(tpcb, "Invalid IP address format\r\n");
            return;
        }
        bool found = false;
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                config.servers[i].ip_addr = ip_val;
                config_save();
                snprintf(resp, sizeof(resp), "Set IP for '%s' to %s (saved)\r\n", name, ip_str);
                send_response(tpcb, resp);
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(resp, sizeof(resp), "Server '%s' not found\r\n", name);
            send_response(tpcb, resp);
        }

    // --- Clear IP ---
    } else if (strncmp(cmd, "clearip ", 8) == 0) {
        const char *name = cmd + 8;
        bool found = false;
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                config.servers[i].ip_addr = 0;
                memset(&monitors[i], 0, sizeof(server_monitor_t));
                config_save();
                snprintf(resp, sizeof(resp), "Cleared IP for '%s' (saved)\r\n", name);
                send_response(tpcb, resp);
                found = true;
                break;
            }
        }
        if (!found) {
            snprintf(resp, sizeof(resp), "Server '%s' not found\r\n", name);
            send_response(tpcb, resp);
        }

    // --- Status ---
    } else if (strcmp(cmd, "status") == 0) {
        snprintf(resp, sizeof(resp), "Autowake: %s | Servers: %d\r\n",
                 config.autowake ? "ON" : "OFF", config.server_count);
        send_response(tpcb, resp);

        for (int i = 0; i < config.server_count; i++) {
            if (!config.servers[i].active) continue;
            char mac_str[18];
            mac_to_str(config.servers[i].mac, mac_str);

            if (config.servers[i].ip_addr != 0) {
                char ip_str[16];
                ip_to_str(config.servers[i].ip_addr, ip_str);
                if (monitors[i].wol_active) {
                    snprintf(resp, sizeof(resp),
                             "  %s (%s) %s WAKING [attempt %d/%d, ping %d/%d]\r\n",
                             config.servers[i].name, mac_str, ip_str,
                             monitors[i].wol_count, WOL_MAX_ATTEMPTS,
                             monitors[i].ping_count, WOL_PINGS_PER_ATTEMPT);
                } else {
                    const char *state = monitors[i].up ? "UP" : "DOWN";
                    snprintf(resp, sizeof(resp), "  %s (%s) %s [%s]\r\n",
                             config.servers[i].name, mac_str, ip_str, state);
                }
            } else {
                snprintf(resp, sizeof(resp), "  %s (%s) [no IP]\r\n",
                         config.servers[i].name, mac_str);
            }
            send_response(tpcb, resp);
        }

    // --- Autowake ---
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

    // --- Schedule commands ---
    } else if (strncmp(cmd, "schedule add ", 13) == 0) {
        // schedule add <days> <HH:MM> <name|all>
        char days_str[16], time_str[8], target[NAME_LEN];
        if (sscanf(cmd + 13, "%15s %7s %31s", days_str, time_str, target) != 3) {
            send_response(tpcb, "Usage: schedule add <days> <HH:MM> <name|all>\r\n"
                                "  days: mtwhfsu or 'daily'\r\n");
            return;
        }
        int hour, minute;
        if (sscanf(time_str, "%d:%d", &hour, &minute) != 2 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            send_response(tpcb, "Invalid time format (use HH:MM)\r\n");
            return;
        }
        uint8_t day_mask = 0;
        if (strcmp(days_str, "daily") == 0) {
            day_mask = 0x7F;
        } else {
            for (const char *c = days_str; *c; c++) {
                switch (*c) {
                    case 'u': day_mask |= (1 << 0); break; // Sunday
                    case 'm': day_mask |= (1 << 1); break; // Monday
                    case 't': day_mask |= (1 << 2); break; // Tuesday
                    case 'w': day_mask |= (1 << 3); break; // Wednesday
                    case 'h': day_mask |= (1 << 4); break; // Thursday
                    case 'f': day_mask |= (1 << 5); break; // Friday
                    case 's': day_mask |= (1 << 6); break; // Saturday
                    default:
                        snprintf(resp, sizeof(resp), "Unknown day char '%c' (use mtwhfsu)\r\n", *c);
                        send_response(tpcb, resp);
                        return;
                }
            }
        }
        int8_t server_idx = -1;
        if (strcmp(target, "all") != 0) {
            bool found = false;
            for (int i = 0; i < config.server_count; i++) {
                if (config.servers[i].active && strcmp(config.servers[i].name, target) == 0) {
                    server_idx = (int8_t)i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                snprintf(resp, sizeof(resp), "Server '%s' not found\r\n", target);
                send_response(tpcb, resp);
                return;
            }
        }
        if (config.schedule_count >= MAX_SCHEDULES) {
            send_response(tpcb, "Schedule list full (max 16)\r\n");
            return;
        }
        schedule_entry_t *sched = &config.schedules[config.schedule_count];
        sched->day_mask = day_mask;
        sched->hour = (uint8_t)hour;
        sched->minute = (uint8_t)minute;
        sched->server_idx = server_idx;
        config.schedule_count++;
        config_save();
        snprintf(resp, sizeof(resp), "Schedule added [%d] (saved)\r\n", config.schedule_count - 1);
        send_response(tpcb, resp);

    } else if (strcmp(cmd, "schedule list") == 0) {
        if (config.schedule_count == 0) {
            send_response(tpcb, "No schedules configured.\r\n");
        } else {
            send_response(tpcb, "Schedules:\r\n");
            for (int i = 0; i < config.schedule_count; i++) {
                schedule_entry_t *sched = &config.schedules[i];
                char days[8] = "";
                int d = 0;
                if (sched->day_mask == 0x7F) {
                    strcpy(days, "daily");
                } else {
                    const char letters[] = "umtwhfs";
                    for (int b = 0; b < 7; b++) {
                        if (sched->day_mask & (1 << b)) days[d++] = letters[b];
                    }
                    days[d] = '\0';
                }
                const char *target_name = "all";
                if (sched->server_idx >= 0 && sched->server_idx < config.server_count) {
                    target_name = config.servers[sched->server_idx].name;
                }
                snprintf(resp, sizeof(resp), "  [%d] %s %02d:%02d → %s\r\n",
                         i, days, sched->hour, sched->minute, target_name);
                send_response(tpcb, resp);
            }
        }

    } else if (strncmp(cmd, "schedule remove ", 16) == 0) {
        int idx = atoi(cmd + 16);
        if (idx < 0 || idx >= config.schedule_count) {
            send_response(tpcb, "Invalid schedule index\r\n");
            return;
        }
        for (int j = idx; j < config.schedule_count - 1; j++) {
            config.schedules[j] = config.schedules[j + 1];
        }
        config.schedule_count--;
        memset(&config.schedules[config.schedule_count], 0, sizeof(schedule_entry_t));
        config_save();
        snprintf(resp, sizeof(resp), "Schedule [%d] removed (saved)\r\n", idx);
        send_response(tpcb, resp);

    // --- Timezone ---
    } else if (strncmp(cmd, "timezone ", 9) == 0) {
        int offset = atoi(cmd + 9);
        if (offset < -720 || offset > 840) {
            send_response(tpcb, "Invalid offset (-720 to +840 minutes)\r\n");
            return;
        }
        config.utc_offset_minutes = (int16_t)offset;
        config_save();
        snprintf(resp, sizeof(resp), "UTC offset set to %d minutes (saved)\r\n", offset);
        send_response(tpcb, resp);

    } else if (strcmp(cmd, "timezone") == 0) {
        snprintf(resp, sizeof(resp), "UTC offset: %d minutes\r\n", config.utc_offset_minutes);
        send_response(tpcb, resp);

    // --- NTP status ---
    } else if (strcmp(cmd, "ntp status") == 0) {
        snprintf(resp, sizeof(resp), "NTP server: %s\r\nSynced: %s\r\n",
                 config.ntp_server, scheduler_ntp_synced() ? "yes" : "no");
        send_response(tpcb, resp);

    // --- MQTT commands ---
    } else if (strncmp(cmd, "mqtt host ", 10) == 0) {
        strncpy(config.mqtt.host, cmd + 10, HOST_LEN - 1);
        config_save();
        snprintf(resp, sizeof(resp), "MQTT host set to '%s' (saved)\r\n", config.mqtt.host);
        send_response(tpcb, resp);

    } else if (strncmp(cmd, "mqtt port ", 10) == 0) {
        config.mqtt.port = (uint16_t)atoi(cmd + 10);
        config_save();
        snprintf(resp, sizeof(resp), "MQTT port set to %d (saved)\r\n", config.mqtt.port);
        send_response(tpcb, resp);

    } else if (strncmp(cmd, "mqtt user ", 10) == 0) {
        strncpy(config.mqtt.user, cmd + 10, CRED_LEN - 1);
        config_save();
        send_response(tpcb, "MQTT user set (saved)\r\n");

    } else if (strncmp(cmd, "mqtt pass ", 10) == 0) {
        strncpy(config.mqtt.pass, cmd + 10, CRED_LEN - 1);
        config_save();
        send_response(tpcb, "MQTT pass set (saved)\r\n");

    } else if (strcmp(cmd, "mqtt on") == 0) {
        config.mqtt.enabled = true;
        config_save();
        bae_mqtt_init();
        send_response(tpcb, "MQTT enabled (saved)\r\n");

    } else if (strcmp(cmd, "mqtt off") == 0) {
        config.mqtt.enabled = false;
        config_save();
        send_response(tpcb, "MQTT disabled (saved)\r\n");

    } else if (strcmp(cmd, "mqtt status") == 0 || strcmp(cmd, "mqtt") == 0) {
        snprintf(resp, sizeof(resp), "MQTT: %s | Host: %s:%d | Connected: %s\r\n",
                 config.mqtt.enabled ? "ON" : "OFF",
                 config.mqtt.host, config.mqtt.port,
                 bae_mqtt_connected() ? "yes" : "no");
        send_response(tpcb, resp);

    // --- Webhook commands ---
    } else if (strncmp(cmd, "webhook url ", 12) == 0) {
        strncpy(config.webhook.url, cmd + 12, URL_LEN - 1);
        config_save();
        snprintf(resp, sizeof(resp), "Webhook URL set (saved)\r\n");
        send_response(tpcb, resp);

    } else if (strcmp(cmd, "webhook on") == 0) {
        config.webhook.enabled = true;
        config_save();
        send_response(tpcb, "Webhook enabled (saved)\r\n");

    } else if (strcmp(cmd, "webhook off") == 0) {
        config.webhook.enabled = false;
        config_save();
        send_response(tpcb, "Webhook disabled (saved)\r\n");

    } else if (strcmp(cmd, "webhook status") == 0 || strcmp(cmd, "webhook") == 0) {
        snprintf(resp, sizeof(resp), "Webhook: %s | URL: %s\r\n",
                 config.webhook.enabled ? "ON" : "OFF", config.webhook.url);
        send_response(tpcb, resp);

    // --- Discover ---
    } else if (strncmp(cmd, "discover ", 9) == 0) {
        const char *ip_str = cmd + 9;
        uint32_t ip_val;
        if (!parse_ip(ip_str, &ip_val)) {
            send_response(tpcb, "Invalid IP address\r\n");
            return;
        }
        // Check if a previous result is ready
        discover_state_t ds = discovery_state();
        if (ds == DISCOVER_FOUND) {
            char mac_str[18];
            mac_to_str(discovery_result_mac(), mac_str);
            snprintf(resp, sizeof(resp), "Last result: %s\r\nStarting new discovery...\r\n", mac_str);
            send_response(tpcb, resp);
        } else {
            send_response(tpcb, "Discovering MAC (poll with 'discover status')...\r\n");
        }
        discovery_start(ip_val);

    } else if (strcmp(cmd, "discover status") == 0) {
        discover_state_t ds = discovery_state();
        if (ds == DISCOVER_FOUND) {
            char mac_str[18];
            mac_to_str(discovery_result_mac(), mac_str);
            snprintf(resp, sizeof(resp), "Found: %s\r\n", mac_str);
        } else if (ds == DISCOVER_PENDING) {
            snprintf(resp, sizeof(resp), "Pending...\r\n");
        } else if (ds == DISCOVER_NOT_FOUND) {
            snprintf(resp, sizeof(resp), "Not found\r\n");
        } else {
            snprintf(resp, sizeof(resp), "Idle (use 'discover <ip>' first)\r\n");
        }
        send_response(tpcb, resp);

    // --- Save / Factory / Help ---
    } else if (strcmp(cmd, "save") == 0) {
        config_save();
        send_response(tpcb, "Config saved to flash\r\n");

    } else if (strcmp(cmd, "factory") == 0) {
        config_load_defaults();
        memset(monitors, 0, sizeof(monitors));
        config_save();
        send_response(tpcb, "Reset to factory defaults (saved)\r\n");

    } else if (strcmp(cmd, "help") == 0) {
        send_response(tpcb,
            "Commands:\r\n"
            "  wake all              - wake all servers\r\n"
            "  wake <name>           - wake a specific server\r\n"
            "  list                  - show configured servers\r\n"
            "  status                - monitoring summary\r\n"
            "  add <name> <mac> [ip] - add/update a server\r\n"
            "  remove <name>         - remove a server\r\n"
            "  setip <name> <ip>     - set server IP for monitoring\r\n"
            "  clearip <name>        - remove server IP\r\n"
            "  autowake on|off       - set auto-wake on boot\r\n"
            "  autowake              - show auto-wake setting\r\n"
            "  schedule add <days> <HH:MM> <name|all>\r\n"
            "  schedule list         - show schedules\r\n"
            "  schedule remove <idx> - remove a schedule\r\n"
            "  timezone [offset]     - get/set UTC offset (minutes)\r\n"
            "  ntp status            - NTP sync status\r\n"
            "  mqtt [on|off|status]  - MQTT control\r\n"
            "  mqtt host|port|user|pass <val>\r\n"
            "  webhook [on|off|status] - webhook control\r\n"
            "  webhook url <url>     - set webhook URL\r\n"
            "  discover <ip>         - discover MAC address\r\n"
            "  discover status       - check discovery result\r\n"
            "  save                  - save config to flash\r\n"
            "  factory               - reset to defaults\r\n"
            "  help                  - this message\r\n");

    } else {
        send_response(tpcb, "Unknown command. Type 'help' for options.\r\n");
    }
}

static err_t server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    cmd_server_t *state = (cmd_server_t *)arg;
    (void)err;

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

    send_response(client_pcb, "BaconAndEggs v2. Type 'help' for commands.\r\n");
    return ERR_OK;
}

bool tcp_cmd_init(void) {
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("Failed to create PCB\n");
        return false;
    }

    if (tcp_bind(pcb, NULL, TCP_PORT)) {
        printf("Failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    cmd_state.server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!cmd_state.server_pcb) {
        printf("Failed to listen\n");
        tcp_close(pcb);
        return false;
    }

    tcp_arg(cmd_state.server_pcb, &cmd_state);
    tcp_accept(cmd_state.server_pcb, server_accept);

    printf("TCP command server on %s:%u\n",
           ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);
    return true;
}
