#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Persistent configuration (NVS namespace "dsk"): Wi-Fi credentials and the
 * DeepSeek API key. */

esp_err_t config_store_init(void); /* init NVS flash once */

bool config_store_has_wifi(void);

esp_err_t config_store_get_wifi(char *ssid, size_t ssid_size, char *pass, size_t pass_size);
esp_err_t config_store_set_wifi(const char *ssid, const char *pass);

esp_err_t config_store_get_api_key(char *buf, size_t size);
esp_err_t config_store_set_api_key(const char *key);

/* Erase Wi-Fi credentials and API key (factory reset). */
esp_err_t config_store_erase_all(void);

/* Basic format check for Wi-Fi credentials.
 * On failure, err is filled with a human readable UTF-8 (Chinese) reason. */
bool config_validate_creds(const char *ssid, const char *pass, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif
