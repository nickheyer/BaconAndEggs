#include "util.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "lwip/ip_addr.h"

bool parse_mac(const char *str, uint8_t mac[6]) {
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

void mac_to_str(const uint8_t mac[6], char *out) {
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void ip_to_str(uint32_t addr, char *out) {
    ip_addr_t ip;
    ip_addr_set_ip4_u32(&ip, addr);
    strcpy(out, ipaddr_ntoa(&ip));
}

bool parse_ip(const char *str, uint32_t *out) {
    ip_addr_t addr;
    if (ipaddr_aton(str, &addr)) {
        *out = ip_addr_get_ip4_u32(&addr);
        return true;
    }
    return false;
}

void trim(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r' || str[len - 1] == ' ')) {
        str[--len] = '\0';
    }
    for (int i = 0; str[i]; i++) {
        str[i] = tolower((unsigned char)str[i]);
    }
}
