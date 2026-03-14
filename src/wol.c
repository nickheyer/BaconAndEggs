#include "wol.h"
#include "util.h"
#include "webhook.h"

#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4_addr.h"

static struct raw_pcb *ping_pcb = NULL;
static uint16_t ping_global_seq = 0;

static u8_t ping_recv_callback(void *arg, struct raw_pcb *pcb, struct pbuf *p,
                                const ip_addr_t *addr) {
    (void)arg;
    (void)pcb;

    if (p->tot_len < sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr))
        return 0;

    struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
    uint16_t hlen = IPH_HL(iphdr) * 4;

    if (p->tot_len < hlen + sizeof(struct icmp_echo_hdr))
        return 0;

    struct icmp_echo_hdr *iecho;
    if (hlen == p->len) {
        struct pbuf *q = p->next;
        if (!q || q->len < sizeof(struct icmp_echo_hdr))
            return 0;
        iecho = (struct icmp_echo_hdr *)q->payload;
    } else {
        iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + hlen);
    }

    if (ICMPH_TYPE(iecho) != ICMP_ER)
        return 0;
    if (ntohs(iecho->id) != PING_ID)
        return 0;

    uint16_t seq = ntohs(iecho->seqno);
    uint32_t src_ip = ip_addr_get_ip4_u32(addr);

    for (int i = 0; i < config.server_count; i++) {
        if (config.servers[i].active && config.servers[i].ip_addr != 0 &&
            config.servers[i].ip_addr == src_ip &&
            monitors[i].ping_pending && monitors[i].ping_seq == seq) {

            monitors[i].ping_pending = false;
            bool was_down = !monitors[i].up;
            monitors[i].up = true;
            if (monitors[i].wol_active) {
                printf("  '%s' responded to ping — WoL verified!\n", config.servers[i].name);
                monitors[i].wol_active = false;
                monitors[i].wol_count = 0;
                monitors[i].ping_count = 0;
            }
            if (was_down) {
                webhook_notify("server_up", config.servers[i].name, "ping response");
            }
            return 1;
        }
    }

    return 0;
}

void ping_init(void) {
    ping_pcb = raw_new(IP_PROTO_ICMP);
    if (!ping_pcb) {
        printf("Failed to create ping PCB\n");
        return;
    }
    raw_recv(ping_pcb, ping_recv_callback, NULL);
    raw_bind(ping_pcb, IP_ADDR_ANY);
    printf("ICMP ping subsystem initialized\n");
}

void ping_send(int server_idx) {
    server_entry_t *s = &config.servers[server_idx];
    server_monitor_t *m = &monitors[server_idx];

    if (s->ip_addr == 0 || !ping_pcb)
        return;

    uint16_t seq = ++ping_global_seq;
    size_t pkt_size = sizeof(struct icmp_echo_hdr) + PING_DATA_SIZE;

    struct pbuf *p = pbuf_alloc(PBUF_IP, (u16_t)pkt_size, PBUF_RAM);
    if (!p)
        return;

    struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)p->payload;
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->id = htons(PING_ID);
    iecho->seqno = htons(seq);
    iecho->chksum = 0;

    uint8_t *data = (uint8_t *)p->payload + sizeof(struct icmp_echo_hdr);
    for (size_t i = 0; i < PING_DATA_SIZE; i++) {
        data[i] = (uint8_t)('A' + (i % 26));
    }

    iecho->chksum = inet_chksum(iecho, (u16_t)pkt_size);

    ip_addr_t dest;
    ip_addr_set_ip4_u32(&dest, s->ip_addr);

    cyw43_arch_lwip_begin();
    raw_sendto(ping_pcb, p, &dest);
    cyw43_arch_lwip_end();

    pbuf_free(p);

    m->ping_pending = true;
    m->ping_seq = seq;
    m->last_ping_ms = to_ms_since_boot(get_absolute_time());
}

bool send_wol_packet(const uint8_t mac[6]) {
    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(&packet[6 + i * 6], mac, 6);
    }

    cyw43_arch_lwip_begin();

    struct udp_pcb *pcb = udp_new();
    if (!pcb) {
        cyw43_arch_lwip_end();
        printf("Failed to create UDP PCB\n");
        return false;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(packet), PBUF_RAM);
    if (!p) {
        printf("Failed to alloc pbuf\n");
        udp_remove(pcb);
        cyw43_arch_lwip_end();
        return false;
    }

    memcpy(p->payload, packet, sizeof(packet));

    ip_addr_t broadcast;
    IP4_ADDR(&broadcast, 255, 255, 255, 255);

    err_t err = udp_sendto(pcb, p, &broadcast, WOL_PORT);
    pbuf_free(p);
    udp_remove(pcb);

    cyw43_arch_lwip_end();

    return err == ERR_OK;
}

void wake_server(const server_entry_t *s) {
    char mac_str[18];
    mac_to_str(s->mac, mac_str);
    printf("Waking '%s' (%s)...\n", s->name, mac_str);

    if (send_wol_packet(s->mac)) {
        printf("  WOL sent OK\n");
        webhook_notify("wol_sent", s->name, "magic packet sent");
    } else {
        printf("  WOL send FAILED\n");
        webhook_notify("wol_failed", s->name, "packet send error");
    }
}

void wake_server_monitored(int idx) {
    server_entry_t *s = &config.servers[idx];
    server_monitor_t *m = &monitors[idx];

    wake_server(s);

    if (s->ip_addr != 0) {
        m->wol_active = true;
        m->wol_count = 1;
        m->ping_count = 0;
        m->up = false;
        ping_send(idx);
        printf("  Ping verification started for '%s'\n", s->name);
    }
}

void wake_all(void) {
    printf("Waking all servers...\n");
    for (int i = 0; i < config.server_count; i++) {
        if (config.servers[i].active) {
            wake_server_monitored(i);
            sleep_ms(100);
        }
    }
}

void monitor_tick(uint32_t now) {
    for (int i = 0; i < config.server_count; i++) {
        server_entry_t *s = &config.servers[i];
        server_monitor_t *m = &monitors[i];

        if (!s->active || s->ip_addr == 0)
            continue;

        // 1. Ping timeout check
        if (m->ping_pending && (now - m->last_ping_ms) >= PING_TIMEOUT_MS) {
            m->ping_pending = false;
            bool was_up = m->up;
            m->up = false;

            if (was_up) {
                webhook_notify("server_down", s->name, "ping timeout");
            }

            if (was_up && !m->wol_active && config.autowake) {
                printf("Health: '%s' went down, auto-waking\n", s->name);
                wake_server_monitored(i);
            }
        }

        // 2. WoL retry state machine
        if (m->wol_active) {
            if (!m->ping_pending && (now - m->last_ping_ms) >= WOL_PING_INTERVAL_MS) {
                m->ping_count++;

                if (m->ping_count >= WOL_PINGS_PER_ATTEMPT) {
                    if (m->wol_count >= WOL_MAX_ATTEMPTS) {
                        printf("  '%s': gave up after %d WoL attempts\n", s->name, m->wol_count);
                        m->wol_active = false;
                        m->wol_count = 0;
                        m->ping_count = 0;
                    } else {
                        m->wol_count++;
                        m->ping_count = 0;
                        printf("  '%s': re-sending WoL (attempt %d/%d)\n",
                               s->name, m->wol_count, WOL_MAX_ATTEMPTS);
                        wake_server(s);
                        ping_send(i);
                    }
                } else {
                    ping_send(i);
                }
            }
            continue;
        }

        // 3. Health check — ping every 5 min
        if ((now - m->last_health_ms) >= HEALTH_INTERVAL_MS) {
            m->last_health_ms = now;
            if (!m->ping_pending) {
                ping_send(i);
            }
        }
    }
}
