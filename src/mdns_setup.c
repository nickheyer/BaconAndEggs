#include "mdns_setup.h"

#include <stdio.h>

#include "pico/cyw43_arch.h"
#include "lwip/apps/mdns.h"
#include "lwip/netif.h"

void mdns_setup_init(void) {
    cyw43_arch_lwip_begin();
    mdns_resp_init();
    mdns_resp_add_netif(netif_list, "baconandeggs");
    mdns_resp_add_service(netif_list, "BaconAndEggs", "_http",
                          DNSSD_PROTO_TCP, 80, NULL, NULL);
    cyw43_arch_lwip_end();

    printf("mDNS: baconandeggs.local\n");
}
