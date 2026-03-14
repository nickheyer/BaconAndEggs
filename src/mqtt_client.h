#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

void bae_mqtt_init(void);
void bae_mqtt_tick(uint32_t now_ms);
bool bae_mqtt_connected(void);
void bae_mqtt_publish_state(const char *server_name, const char *state);

#endif
