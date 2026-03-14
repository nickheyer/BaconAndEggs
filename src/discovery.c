#include "discovery.h"
#include "util.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4_addr.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"

#define DISCOVER_PING_ID   0xBE02
#define DISCOVER_TIMEOUT_MS 2000

static uint32_t discover_target_ip = 0;
static uint32_t discover_start_ms = 0;
static discover_state_t state = DISCOVER_IDLE;
static uint8_t discover_retries = 0;
static uint8_t result_mac[6];

static void discover_send_ping(uint32_t ip) {
    size_t pkt_size = sizeof(struct icmp_echo_hdr) + 8;

    struct pbuf *p = pbuf_alloc(PBUF_IP, (u16_t)pkt_size, PBUF_RAM);
    if (!p) return;

    struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->id = htons(DISCOVER_PING_ID);
    iecho->seqno = htons(1);
    iecho->chksum = 0;

    memset((uint8_t *)p->payload + sizeof(struct icmp_echo_hdr), 0xAA, 8);
    iecho->chksum = inet_chksum(iecho, (u16_t)pkt_size);

    ip_addr_t dest;
    ip_addr_set_ip4_u32(&dest, ip);

    cyw43_arch_lwip_begin();

    struct raw_pcb *pcb = raw_new(IP_PROTO_ICMP);
    if (pcb) {
        raw_sendto(pcb, p, &dest);
        raw_remove(pcb);
    }

    cyw43_arch_lwip_end();
    pbuf_free(p);
}

static bool discover_check_arp(uint32_t ip, uint8_t *mac_out) {
    ip4_addr_t ip4;
    ip4.addr = ip;

    struct eth_addr *ethaddr = NULL;
    const ip4_addr_t *ret_ip = NULL;

    cyw43_arch_lwip_begin();
    int idx = etharp_find_addr(netif_list, &ip4, &ethaddr, &ret_ip);
    cyw43_arch_lwip_end();

    if (idx >= 0 && ethaddr) {
        memcpy(mac_out, ethaddr->addr, 6);
        return true;
    }
    return false;
}

void discovery_start(uint32_t ip_addr) {
    discover_target_ip = ip_addr;
    discover_retries = 0;
    state = DISCOVER_PENDING;
    discover_start_ms = to_ms_since_boot(get_absolute_time());

    char ip_str[16];
    ip_to_str(ip_addr, ip_str);
    printf("Discovery: pinging %s...\n", ip_str);

    discover_send_ping(ip_addr);
}

discover_state_t discovery_state(void) {
    return state;
}

const uint8_t *discovery_result_mac(void) {
    return result_mac;
}

void discovery_tick(uint32_t now_ms) {
    if (state != DISCOVER_PENDING)
        return;

    if ((now_ms - discover_start_ms) < DISCOVER_TIMEOUT_MS)
        return;

    if (discover_check_arp(discover_target_ip, result_mac)) {
        char mac_str[18];
        mac_to_str(result_mac, mac_str);
        printf("Discovery: found MAC %s\n", mac_str);
        state = DISCOVER_FOUND;
        return;
    }

    if (discover_retries < 1) {
        discover_retries++;
        discover_start_ms = now_ms;
        discover_send_ping(discover_target_ip);
        printf("Discovery: retrying...\n");
        return;
    }

    printf("Discovery: not found\n");
    state = DISCOVER_NOT_FOUND;
}
