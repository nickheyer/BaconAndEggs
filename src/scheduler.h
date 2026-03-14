#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

void scheduler_init(void);
void scheduler_tick(uint32_t now_ms);
bool scheduler_ntp_synced(void);

#endif
