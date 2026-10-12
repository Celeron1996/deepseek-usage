#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start SNTP (idempotent) and set the timezone to China Standard Time.
 * Call it whenever the station gets an IP. */
void ntp_time_start_once(void);

/* True once the clock looks sane (used to gate TLS calls: certificate date check). */
bool ntp_time_synced(void);

#ifdef __cplusplus
}
#endif
