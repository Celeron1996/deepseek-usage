#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the LVGL screen (header, list and status bar).
 */
void ui_init(void);

/**
 * @brief Replace the list content with the scanned APs.
 */
void ui_update_ap_list(const wifi_ap_record_t *records, uint16_t count);

/**
 * @brief Update the bottom status text.
 */
void ui_set_status(const char *text);

/**
 * @brief Show/hide the scanning spinner.
 */
void ui_set_scanning(bool scanning);

#ifdef __cplusplus
}
#endif
