#include "ntp_time.h"

#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "ntp";

static void time_sync_cb(struct timeval *tv)
{
    time_t now = tv->tv_sec;
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    ESP_LOGI(TAG, "time synced: %04d-%02d-%02d %02d:%02d:%02d",
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

bool ntp_time_synced(void)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return (tm_now.tm_year + 1900) >= 2024;
}

void ntp_time_start_once(void)
{
    static bool started = false;
    if (started) {
        return;
    }

    /* China Standard Time (UTC+8) */
    setenv("TZ", "CST-8", 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "pool.ntp.org"));
    cfg.start = true;
    cfg.smooth_sync = false;
    cfg.sync_cb = time_sync_cb;

    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err == ESP_OK) {
        started = true;
        ESP_LOGI(TAG, "sntp started (ntp.aliyun.com, pool.ntp.org)");
    } else if (err == ESP_ERR_INVALID_STATE) {
        started = true; /* already running */
    } else {
        ESP_LOGW(TAG, "sntp start failed: %s", esp_err_to_name(err));
    }
}
