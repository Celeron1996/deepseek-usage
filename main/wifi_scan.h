#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init Wi-Fi in station mode (no connection is made, scanning only).
 */
esp_err_t wifi_scan_init(void);

/**
 * @brief Perform a blocking scan of all channels.
 *
 * @param[out] ap_records  Caller-owned array with the results (free() it), NULL when none.
 * @param[out] ap_count    Number of APs found.
 */
esp_err_t wifi_scan_run(wifi_ap_record_t **ap_records, uint16_t *ap_count);

#ifdef __cplusplus
}
#endif
