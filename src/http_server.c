#include "http_server.h"
#include "config.h"
#include "util.h"
#include "wol.h"
#include "scheduler.h"
#include "mqtt_client.h"
#include "webhook.h"
#include "discovery.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/fs.h"

// --- JSON API response buffer ---
static char api_buf[2048];
static int api_len = 0;

// --- POST handling ---
static char post_uri[64];
static char post_body[512];
static int post_body_len = 0;
static char post_response[512];
static int post_response_len = 0;

// --- Minimal JSON helpers ---
// Find "key": in body and extract string value
static bool json_get_string(const char *body, const char *key, char *out, int max_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(body, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < max_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

// Extract integer value for "key":
static bool json_get_int(const char *body, const char *key, int *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(body, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        *out = atoi(p);
        return true;
    }
    return false;
}

// Extract bool value for "key":
static bool json_get_bool(const char *body, const char *key, bool *out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(body, pattern);
    if (!p) return false;
    p += strlen(pattern);
    while (*p == ' ') p++;
    if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

// --- GET API via fs_open_custom ---

static void build_status_json(void) {
    int pos = 0;
    uint32_t uptime = to_ms_since_boot(get_absolute_time()) / 1000;

    pos += snprintf(api_buf + pos, sizeof(api_buf) - pos,
        "{\"autowake\":%s,\"uptime\":%u,\"ntp_synced\":%s,"
        "\"mqtt_connected\":%s,\"server_count\":%d,\"servers\":[",
        config.autowake ? "true" : "false",
        uptime,
        scheduler_ntp_synced() ? "true" : "false",
        bae_mqtt_connected() ? "true" : "false",
        config.server_count);

    for (int i = 0; i < config.server_count && pos < (int)sizeof(api_buf) - 128; i++) {
        if (!config.servers[i].active) continue;
        char mac_str[18];
        mac_to_str(config.servers[i].mac, mac_str);

        const char *state = monitors[i].wol_active ? "waking" :
                            monitors[i].up ? "up" : "down";

        char ip_str[16] = "";
        if (config.servers[i].ip_addr != 0) {
            ip_to_str(config.servers[i].ip_addr, ip_str);
        }

        if (i > 0) pos += snprintf(api_buf + pos, sizeof(api_buf) - pos, ",");
        pos += snprintf(api_buf + pos, sizeof(api_buf) - pos,
            "{\"name\":\"%s\",\"mac\":\"%s\",\"ip\":\"%s\",\"state\":\"%s\","
            "\"wol_count\":%d,\"ping_count\":%d}",
            config.servers[i].name, mac_str, ip_str, state,
            monitors[i].wol_count, monitors[i].ping_count);
    }

    pos += snprintf(api_buf + pos, sizeof(api_buf) - pos, "]}");
    api_len = pos;
}

static void build_schedules_json(void) {
    int pos = 0;
    pos += snprintf(api_buf + pos, sizeof(api_buf) - pos,
        "{\"utc_offset\":%d,\"schedules\":[", config.utc_offset_minutes);

    for (int i = 0; i < config.schedule_count && pos < (int)sizeof(api_buf) - 128; i++) {
        schedule_entry_t *s = &config.schedules[i];
        const char *target = "all";
        if (s->server_idx >= 0 && s->server_idx < config.server_count)
            target = config.servers[s->server_idx].name;

        if (i > 0) pos += snprintf(api_buf + pos, sizeof(api_buf) - pos, ",");
        pos += snprintf(api_buf + pos, sizeof(api_buf) - pos,
            "{\"index\":%d,\"day_mask\":%d,\"hour\":%d,\"minute\":%d,\"target\":\"%s\"}",
            i, s->day_mask, s->hour, s->minute, target);
    }

    pos += snprintf(api_buf + pos, sizeof(api_buf) - pos, "]}");
    api_len = pos;
}

static void build_mqtt_json(void) {
    api_len = snprintf(api_buf, sizeof(api_buf),
        "{\"enabled\":%s,\"host\":\"%s\",\"port\":%d,\"user\":\"%s\",\"connected\":%s}",
        config.mqtt.enabled ? "true" : "false",
        config.mqtt.host, config.mqtt.port, config.mqtt.user,
        bae_mqtt_connected() ? "true" : "false");
}

static void build_webhook_json(void) {
    api_len = snprintf(api_buf, sizeof(api_buf),
        "{\"enabled\":%s,\"url\":\"%s\"}",
        config.webhook.enabled ? "true" : "false",
        config.webhook.url);
}

static void build_time_json(void) {
    api_len = snprintf(api_buf, sizeof(api_buf),
        "{\"ntp_synced\":%s,\"ntp_server\":\"%s\",\"utc_offset\":%d}",
        scheduler_ntp_synced() ? "true" : "false",
        config.ntp_server, config.utc_offset_minutes);
}

static void build_discover_json(void) {
    discover_state_t ds = discovery_state();
    if (ds == DISCOVER_FOUND) {
        char mac_str[18];
        mac_to_str(discovery_result_mac(), mac_str);
        api_len = snprintf(api_buf, sizeof(api_buf),
            "{\"state\":\"found\",\"mac\":\"%s\"}", mac_str);
    } else if (ds == DISCOVER_PENDING) {
        api_len = snprintf(api_buf, sizeof(api_buf),
            "{\"state\":\"pending\"}");
    } else if (ds == DISCOVER_NOT_FOUND) {
        api_len = snprintf(api_buf, sizeof(api_buf),
            "{\"state\":\"not_found\"}");
    } else {
        api_len = snprintf(api_buf, sizeof(api_buf),
            "{\"state\":\"idle\"}");
    }
}

// Custom file handler for API endpoints
int fs_open_custom(struct fs_file *file, const char *name) {
    if (strncmp(name, "/api/", 5) != 0)
        return 0;

    const char *endpoint = name + 5;

    if (strcmp(endpoint, "status") == 0) {
        build_status_json();
    } else if (strcmp(endpoint, "schedules") == 0) {
        build_schedules_json();
    } else if (strcmp(endpoint, "mqtt") == 0) {
        build_mqtt_json();
    } else if (strcmp(endpoint, "webhook") == 0) {
        build_webhook_json();
    } else if (strcmp(endpoint, "time") == 0) {
        build_time_json();
    } else if (strcmp(endpoint, "discover") == 0) {
        build_discover_json();
    } else if (strcmp(endpoint, "result.json") == 0) {
        // POST result — already in post_response
        memcpy(api_buf, post_response, post_response_len);
        api_len = post_response_len;
    } else {
        return 0;
    }

    memset(file, 0, sizeof(struct fs_file));
    file->data = api_buf;
    file->len = api_len;
    file->index = file->len;
    file->flags = FS_FILE_FLAGS_HEADER_PERSISTENT;
    return 1;
}

void fs_close_custom(struct fs_file *file) {
    (void)file;
}

// --- POST handling ---

err_t httpd_post_begin(void *connection, const char *uri, const char *http_request,
                        u16_t http_request_len, int content_len,
                        char *response_uri, u16_t response_uri_len,
                        u8_t *post_auto_wnd) {
    (void)connection;
    (void)http_request;
    (void)http_request_len;
    (void)content_len;
    (void)post_auto_wnd;

    if (strncmp(uri, "/api/", 5) != 0) {
        return ERR_VAL;
    }

    strncpy(post_uri, uri, sizeof(post_uri) - 1);
    post_body_len = 0;
    post_body[0] = '\0';

    snprintf(response_uri, response_uri_len, "/api/result.json");
    return ERR_OK;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
    (void)connection;

    int copy = p->tot_len;
    if (post_body_len + copy >= (int)sizeof(post_body) - 1) {
        copy = sizeof(post_body) - 1 - post_body_len;
    }
    if (copy > 0) {
        pbuf_copy_partial(p, post_body + post_body_len, copy, 0);
        post_body_len += copy;
        post_body[post_body_len] = '\0';
    }
    pbuf_free(p);
    return ERR_OK;
}

static void post_ok(const char *msg) {
    post_response_len = snprintf(post_response, sizeof(post_response),
        "{\"ok\":true,\"message\":\"%s\"}", msg);
}

static void post_error(const char *msg) {
    post_response_len = snprintf(post_response, sizeof(post_response),
        "{\"ok\":false,\"error\":\"%s\"}", msg);
}

void httpd_post_finished(void *connection, char *response_uri, u16_t response_uri_len) {
    (void)connection;
    snprintf(response_uri, response_uri_len, "/api/result.json");

    const char *ep = post_uri + 5;  // skip "/api/"

    // --- Wake ---
    if (strcmp(ep, "wake") == 0) {
        bool all = false;
        json_get_bool(post_body, "all", &all);
        if (all) {
            wake_all();
            post_ok("Waking all servers");
            return;
        }
        char name[NAME_LEN];
        if (json_get_string(post_body, "name", name, sizeof(name))) {
            for (int i = 0; i < config.server_count; i++) {
                if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                    wake_server_monitored(i);
                    post_ok("WoL sent");
                    return;
                }
            }
            post_error("Server not found");
        } else {
            post_error("Missing name or all");
        }

    // --- Server add ---
    } else if (strcmp(ep, "server/add") == 0) {
        char name[NAME_LEN], mac_str[18], ip_str[16] = "";
        if (!json_get_string(post_body, "name", name, sizeof(name)) ||
            !json_get_string(post_body, "mac", mac_str, sizeof(mac_str))) {
            post_error("Missing name or mac");
            return;
        }
        json_get_string(post_body, "ip", ip_str, sizeof(ip_str));

        uint32_t ip_val = 0;
        if (strlen(ip_str) > 0 && !parse_ip(ip_str, &ip_val)) {
            post_error("Invalid IP");
            return;
        }

        // Check for existing
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                if (!parse_mac(mac_str, config.servers[i].mac)) {
                    post_error("Invalid MAC");
                    return;
                }
                if (strlen(ip_str) > 0) config.servers[i].ip_addr = ip_val;
                config_save();
                post_ok("Updated");
                return;
            }
        }

        if (config.server_count >= MAX_SERVERS) {
            post_error("Server list full");
            return;
        }

        server_entry_t *s = &config.servers[config.server_count];
        if (!parse_mac(mac_str, s->mac)) {
            post_error("Invalid MAC");
            return;
        }
        strncpy(s->name, name, NAME_LEN - 1);
        s->active = true;
        s->ip_addr = ip_val;
        memset(&monitors[config.server_count], 0, sizeof(server_monitor_t));
        config.server_count++;
        config_save();
        post_ok("Added");

    // --- Server remove ---
    } else if (strcmp(ep, "server/remove") == 0) {
        char name[NAME_LEN];
        if (!json_get_string(post_body, "name", name, sizeof(name))) {
            post_error("Missing name");
            return;
        }
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
                post_ok("Removed");
                return;
            }
        }
        post_error("Server not found");

    // --- Server setip ---
    } else if (strcmp(ep, "server/setip") == 0) {
        char name[NAME_LEN], ip_str[16];
        if (!json_get_string(post_body, "name", name, sizeof(name)) ||
            !json_get_string(post_body, "ip", ip_str, sizeof(ip_str))) {
            post_error("Missing name or ip");
            return;
        }
        uint32_t ip_val;
        if (!parse_ip(ip_str, &ip_val)) {
            post_error("Invalid IP");
            return;
        }
        for (int i = 0; i < config.server_count; i++) {
            if (config.servers[i].active && strcmp(config.servers[i].name, name) == 0) {
                config.servers[i].ip_addr = ip_val;
                config_save();
                post_ok("IP set");
                return;
            }
        }
        post_error("Server not found");

    // --- Autowake ---
    } else if (strcmp(ep, "autowake") == 0) {
        bool enabled;
        if (json_get_bool(post_body, "enabled", &enabled)) {
            config.autowake = enabled;
            config_save();
            post_ok(enabled ? "Autowake ON" : "Autowake OFF");
        } else {
            post_error("Missing enabled");
        }

    // --- Schedule add ---
    } else if (strcmp(ep, "schedule/add") == 0) {
        int days, hour, min;
        char target[NAME_LEN];
        if (!json_get_int(post_body, "days", &days) ||
            !json_get_int(post_body, "hour", &hour) ||
            !json_get_int(post_body, "min", &min)) {
            post_error("Missing days, hour, or min");
            return;
        }
        if (!json_get_string(post_body, "target", target, sizeof(target))) {
            strcpy(target, "all");
        }
        if (config.schedule_count >= MAX_SCHEDULES) {
            post_error("Schedule list full");
            return;
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
                post_error("Target server not found");
                return;
            }
        }
        schedule_entry_t *sched = &config.schedules[config.schedule_count];
        sched->day_mask = (uint8_t)days;
        sched->hour = (uint8_t)hour;
        sched->minute = (uint8_t)min;
        sched->server_idx = server_idx;
        config.schedule_count++;
        config_save();
        post_ok("Schedule added");

    // --- Schedule remove ---
    } else if (strcmp(ep, "schedule/remove") == 0) {
        int idx;
        if (!json_get_int(post_body, "index", &idx) || idx < 0 || idx >= config.schedule_count) {
            post_error("Invalid index");
            return;
        }
        for (int j = idx; j < config.schedule_count - 1; j++) {
            config.schedules[j] = config.schedules[j + 1];
        }
        config.schedule_count--;
        config_save();
        post_ok("Schedule removed");

    // --- MQTT config ---
    } else if (strcmp(ep, "mqtt") == 0) {
        json_get_bool(post_body, "enabled", &config.mqtt.enabled);
        json_get_string(post_body, "host", config.mqtt.host, HOST_LEN);
        int port;
        if (json_get_int(post_body, "port", &port)) config.mqtt.port = (uint16_t)port;
        json_get_string(post_body, "user", config.mqtt.user, CRED_LEN);
        json_get_string(post_body, "pass", config.mqtt.pass, CRED_LEN);
        config_save();
        if (config.mqtt.enabled) bae_mqtt_init();
        post_ok("MQTT config saved");

    // --- Webhook config ---
    } else if (strcmp(ep, "webhook") == 0) {
        json_get_bool(post_body, "enabled", &config.webhook.enabled);
        json_get_string(post_body, "url", config.webhook.url, URL_LEN);
        config_save();
        post_ok("Webhook config saved");

    // --- Timezone ---
    } else if (strcmp(ep, "timezone") == 0) {
        int offset;
        if (json_get_int(post_body, "offset", &offset)) {
            config.utc_offset_minutes = (int16_t)offset;
            config_save();
            post_ok("Timezone set");
        } else {
            post_error("Missing offset");
        }

    // --- Discover ---
    } else if (strcmp(ep, "discover") == 0) {
        char ip_str[16];
        if (!json_get_string(post_body, "ip", ip_str, sizeof(ip_str))) {
            post_error("Missing ip");
            return;
        }
        uint32_t ip_val;
        if (!parse_ip(ip_str, &ip_val)) {
            post_error("Invalid IP");
            return;
        }
        discovery_start(ip_val);
        post_ok("Discovery started — poll GET /api/discover for result");

    // --- Factory reset ---
    } else if (strcmp(ep, "factory") == 0) {
        config_load_defaults();
        memset(monitors, 0, sizeof(monitors));
        config_save();
        post_ok("Factory reset complete");

    } else {
        post_error("Unknown endpoint");
    }
}

void http_server_init(void) {
    httpd_init();
    printf("HTTP server on port 80\n");
}
