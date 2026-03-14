#include "scheduler.h"
#include "config.h"
#include "wol.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pico/stdlib.h"
#include "hardware/rtc.h"
#include "pico/util/datetime.h"
#include "lwip/apps/sntp.h"

static bool ntp_synced = false;
static uint32_t last_check_ms = 0;
#define SCHEDULE_CHECK_INTERVAL_MS 60000  // 1 minute

void sntp_set_system_time_cb(uint32_t sec) {
    time_t t = (time_t)sec;
    struct tm *utc = gmtime(&t);

    datetime_t dt;
    dt.year = (int16_t)(utc->tm_year + 1900);
    dt.month = (int8_t)(utc->tm_mon + 1);
    dt.day = (int8_t)utc->tm_mday;
    dt.dotw = (int8_t)utc->tm_wday;
    dt.hour = (int8_t)utc->tm_hour;
    dt.min = (int8_t)utc->tm_min;
    dt.sec = (int8_t)utc->tm_sec;

    rtc_set_datetime(&dt);
    ntp_synced = true;
    printf("NTP synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
           dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec);
}

void scheduler_init(void) {
    rtc_init();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, config.ntp_server);
    sntp_init();

    printf("Scheduler initialized (NTP: %s, UTC offset: %d min)\n",
           config.ntp_server, config.utc_offset_minutes);
}

bool scheduler_ntp_synced(void) {
    return ntp_synced;
}

void scheduler_tick(uint32_t now_ms) {
    if (!ntp_synced || config.schedule_count == 0)
        return;

    if ((now_ms - last_check_ms) < SCHEDULE_CHECK_INTERVAL_MS)
        return;
    last_check_ms = now_ms;

    datetime_t dt;
    if (!rtc_get_datetime(&dt))
        return;

    // Apply UTC offset to get local time
    int total_minutes = dt.hour * 60 + dt.min + config.utc_offset_minutes;
    int local_hour, local_min;
    int day_offset = 0;

    if (total_minutes < 0) {
        total_minutes += 24 * 60;
        day_offset = -1;
    } else if (total_minutes >= 24 * 60) {
        total_minutes -= 24 * 60;
        day_offset = 1;
    }
    local_hour = total_minutes / 60;
    local_min = total_minutes % 60;

    // Adjust day of week
    int dotw = (dt.dotw + day_offset + 7) % 7;  // 0=Sunday

    for (int i = 0; i < config.schedule_count; i++) {
        schedule_entry_t *sched = &config.schedules[i];

        // Check day mask
        if (!(sched->day_mask & (1 << dotw)))
            continue;

        // Check time
        if (sched->hour != (uint8_t)local_hour || sched->minute != (uint8_t)local_min)
            continue;

        // Match!
        if (sched->server_idx < 0) {
            printf("Schedule [%d]: waking all\n", i);
            wake_all();
        } else if (sched->server_idx < config.server_count &&
                   config.servers[sched->server_idx].active) {
            printf("Schedule [%d]: waking '%s'\n", i, config.servers[sched->server_idx].name);
            wake_server_monitored(sched->server_idx);
        }
    }
}
