#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the configuration web server (listens on all interfaces,
 * reachable over the SoftAP and/or the station IP). */
esp_err_t webserver_start(QueueHandle_t event_queue);

/* DNS hijack for captive portal auto-popup, redirects every name to the AP IP.
 * Requires the SoftAP to be up. */
esp_err_t captive_portal_start(const char *ap_ip);
void captive_portal_stop(void);

#ifdef __cplusplus
}
#endif
