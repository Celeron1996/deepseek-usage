#include "config_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "config";

#define NVS_NAMESPACE "dsk"
#define KEY_SSID      "ssid"
#define KEY_PASS      "pass"
#define KEY_API       "key"

esp_err_t config_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

static esp_err_t get_str(const char *key, char *buf, size_t size)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, key, buf, &size);
    nvs_close(h);
    if (err == ESP_OK && buf != NULL && size > 0) {
        buf[size - 1] = '\0';
    }
    return err;
}

static esp_err_t set_str(const char *key, const char *value)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

bool config_store_has_wifi(void)
{
    char ssid[64] = {0};
    if (get_str(KEY_SSID, ssid, sizeof(ssid)) != ESP_OK) {
        return false;
    }
    return ssid[0] != '\0';
}

esp_err_t config_store_get_wifi(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    esp_err_t err = get_str(KEY_SSID, ssid, ssid_size);
    if (err != ESP_OK) {
        return err;
    }
    /* password may be empty (open network) */
    if (get_str(KEY_PASS, pass, pass_size) != ESP_OK && pass_size > 0) {
        pass[0] = '\0';
    }
    return ESP_OK;
}

esp_err_t config_store_set_wifi(const char *ssid, const char *pass)
{
    esp_err_t err = set_str(KEY_SSID, ssid);
    if (err == ESP_OK) {
        err = set_str(KEY_PASS, pass);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "wifi credentials saved (ssid=%s)", ssid);
    }
    return err;
}

esp_err_t config_store_get_api_key(char *buf, size_t size)
{
    esp_err_t err = get_str(KEY_API, buf, size);
    if (err != ESP_OK && size > 0) {
        buf[0] = '\0';
    }
    return err;
}

esp_err_t config_store_set_api_key(const char *key)
{
    return set_str(KEY_API, key);
}

esp_err_t config_store_erase_all(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

bool config_validate_creds(const char *ssid, const char *pass, char *err, size_t err_size)
{
    size_t ssid_len = strlen(ssid);
    size_t pass_len = strlen(pass);

    if (ssid_len == 0) {
        snprintf(err, err_size, "Wi-Fi name cannot be empty");
        return false;
    }
    if (ssid_len > 32) {
        snprintf(err, err_size, "Wi-Fi name too long (max 32 bytes)");
        return false;
    }
    if (pass_len > 0 && pass_len < 8) {
        snprintf(err, err_size, "Wi-Fi password too short (min 8 chars, leave empty for open networks)");
        return false;
    }
    if (pass_len > 63) {
        snprintf(err, err_size, "Wi-Fi password too long (max 63 chars)");
        return false;
    }
    return true;
}
