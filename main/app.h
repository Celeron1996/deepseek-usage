#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Events posted to the application queue by the wifi manager, the web server
 * and the button task. */
typedef enum {
    APP_EVT_WIFI_CREDS_SAVED = 0, /* web: new valid credentials were stored */
    APP_EVT_WIFI_CREDS_INVALID,   /* web: credentials rejected, msg carries the reason */
    APP_EVT_STA_GOT_IP,           /* wifi: station connected and got an IP */
    APP_EVT_STA_DISCONNECTED,     /* wifi: station lost the connection */
    APP_EVT_BUTTON,               /* button: short press */
} app_event_t;

typedef struct {
    app_event_t type;
    char msg[128]; /* optional detail (validation error text) */
} app_msg_t;

#define APP_QUEUE_LEN 10

#ifdef __cplusplus
}
#endif
