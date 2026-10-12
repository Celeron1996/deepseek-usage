#include <stdlib.h>

#include "button.h"
#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "ui.h"
#include "wifi_scan.h"

static const char *TAG = "wifi_demo";

/* Rescan even without a button press */
#define AUTO_RESCAN_TIMEOUT_MS  60000

static void wifi_scanner_task(void *arg)
{
    ESP_ERROR_CHECK(wifi_scan_init());

    while (true) {
        ESP_LOGI(TAG, "scanning...");
        ui_set_scanning(true);
        ui_set_status("Scanning...");

        wifi_ap_record_t *records = NULL;
        uint16_t count = 0;
        esp_err_t err = wifi_scan_run(&records, &count);
        if (err == ESP_OK) {
            ui_update_ap_list(records, count);
            free(records);
        } else {
            ui_set_status("Scan failed");
        }
        ui_set_scanning(false);

        if (button_wait_for_press(AUTO_RESCAN_TIMEOUT_MS)) {
            ESP_LOGI(TAG, "button pressed, rescanning");
        } else {
            ESP_LOGI(TAG, "auto rescan");
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(display_init());
    ui_init();
    ESP_ERROR_CHECK(button_init());

    xTaskCreate(wifi_scanner_task, "wifi_scanner", 4096, NULL, 5, NULL);
}
