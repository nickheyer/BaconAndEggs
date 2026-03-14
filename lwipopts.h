#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#include "lwipopts_examples_common.h"

// Raw PCBs for ping
#define MEMP_NUM_RAW_PCB 4

// HTTP server
#define LWIP_HTTPD_CUSTOM_FILES       1
#define LWIP_HTTPD_SUPPORT_POST       1
#define LWIP_HTTPD_DYNAMIC_HEADERS    1
#define HTTPD_SERVER_AGENT            "BaconAndEggs"
#define HTTPD_FSDATA_FILE             "gen/fsdata_custom.c"

// mDNS
#define LWIP_MDNS_RESPONDER           1
#define MDNS_MAX_SERVICES             1
#define LWIP_IGMP                     1
#define LWIP_NUM_NETIF_CLIENT_DATA    1

// SNTP
#define SNTP_SERVER_DNS               1
#include <stdint.h>
void sntp_set_system_time_cb(uint32_t sec);
#define SNTP_SET_SYSTEM_TIME(sec)     sntp_set_system_time_cb(sec)

// Memory tuning
#undef MEM_SIZE
#define MEM_SIZE                      8000
#define MEMP_NUM_TCP_PCB              8
#undef MEMP_NUM_TCP_SEG
#define MEMP_NUM_TCP_SEG              48
#undef PBUF_POOL_SIZE
#define PBUF_POOL_SIZE                32
#define MEMP_NUM_UDP_PCB              6
#define MEMP_NUM_SYS_TIMEOUT          16

#endif
