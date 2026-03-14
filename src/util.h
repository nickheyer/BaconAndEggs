#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdbool.h>

bool parse_mac(const char *str, uint8_t mac[6]);
void mac_to_str(const uint8_t mac[6], char *out);
void ip_to_str(uint32_t addr, char *out);
bool parse_ip(const char *str, uint32_t *out);
void trim(char *str);

#endif
