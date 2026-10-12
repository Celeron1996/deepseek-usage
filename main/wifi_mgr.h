#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_mgr_init(QueueHandle_t event_queue);

/* Start an open SoftAP (APSTA mode) for provisioning. */
esp_err_t wifi_mgr_start_ap(const char *ssid);

/* Leave APSTA and keep station only (called after a successful connection). */
esp_err_t wifi_mgr_stop_ap(void);

esp_err_t wifi_mgr_set_sta_creds(const char *ssid, const char *pass);

/* Ask the driver to connect to the configured AP. Safe to call repeatedly. */
esp_err_t wifi_mgr_connect(void);

bool wifi_mgr_is_connected(void);

/* Fill buf with the station IP (e.g. "192.168.1.23"), returns false when no IP. */
bool wifi_mgr_get_sta_ip(char *buf, size_t size);

#ifdef __cplusplus
}
#endif
