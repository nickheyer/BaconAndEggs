/**
 * BaconAndEggs v2 — Wake-on-LAN Server for Pico W
 *
 * Features: TCP CLI, Web UI, mDNS, SNTP/RTC scheduling, MQTT,
 * webhooks, auto-discovery, flash-persisted config.
 */

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "config.h"
#include "wol.h"
#include "tcp_cmd.h"
#include "http_server.h"
#include "mdns_setup.h"
#include "scheduler.h"
#include "mqtt_client.h"
#include "discovery.h"

#define WIFI_RETRY_DELAY_MS 5000

int main() {
    stdio_init_all();
    sleep_ms(5000);

    printf("=== BaconAndEggs v2 ===\n");

    if (cyw43_arch_init()) {
        printf("WiFi init failed\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();

    // WiFi retry loop
    printf("Connecting to WiFi...\n");
    while (true) {
        if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD,
                                                CYW43_AUTH_WPA2_AES_PSK, 30000) == 0) {
            break;
        }
        printf("WiFi connect failed, retrying in %d ms...\n", WIFI_RETRY_DELAY_MS);
        sleep_ms(WIFI_RETRY_DELAY_MS);
    }
    printf("Connected. IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

    // Initialize ping subsystem
    cyw43_arch_lwip_begin();
    ping_init();
    cyw43_arch_lwip_end();

    // Load config
    if (!config_load()) {
        printf("No saved config, loading defaults\n");
        config_load_defaults();
        config_save();
    }

    memset(monitors, 0, sizeof(monitors));

    // Init subsystems
    mdns_setup_init();
    scheduler_init();
    bae_mqtt_init();

    if (!tcp_cmd_init()) {
        return 1;
    }

    http_server_init();

    // Autowake
    if (config.autowake) {
        printf("--- Auto-wake on boot ---\n");
        wake_all();
    } else {
        printf("Auto-wake disabled, skipping\n");
    }

    printf("=== Ready ===\n");

    // Main loop
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        monitor_tick(now);
        scheduler_tick(now);
        bae_mqtt_tick(now);
        discovery_tick(now);

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
