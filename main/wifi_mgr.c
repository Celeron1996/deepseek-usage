#include "wifi_mgr.h"

#include <string.h>

#include "app.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_mgr";

static QueueHandle_t s_evt_q;
static volatile bool s_connected;
static esp_netif_t *s_ap_netif;

static void post_event(app_event_t type, const char *msg)
{
    if (!s_evt_q) {
        return;
    }
    app_msg_t m = { .type = type };
    if (msg) {
        strlcpy(m.msg, msg, sizeof(m.msg));
    }
    xQueueSend(s_evt_q, &m, 0);
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_DISCONNECTED: {
            const wifi_event_sta_disconnected_t *d = data;
            ESP_LOGW(TAG, "station disconnected (reason=%d)", d->reason);
            s_connected = false;
            post_event(APP_EVT_STA_DISCONNECTED, NULL);
            break;
        }
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "softap started");
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ip = data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&ip->ip_info.ip));
        s_connected = true;
        post_event(APP_EVT_STA_GOT_IP, NULL);
    }
}

esp_err_t wifi_mgr_init(QueueHandle_t event_queue)
{
    s_evt_q = event_queue;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler, NULL, NULL));

    /* Credentials live in our own NVS namespace, the driver keeps them in RAM
     * so the connection flow stays fully controlled by the application. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t wifi_mgr_start_ap(const char *ssid)
{
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t ap_cfg = {
        .ap = {
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strlcpy((char *)ap_cfg.ap.ssid, ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));

    ESP_LOGI(TAG, "softap configured: ssid=%s (open)", ssid);
    return ESP_OK;
}

esp_err_t wifi_mgr_stop_ap(void)
{
    ESP_LOGI(TAG, "stopping softap, station only");
    return esp_wifi_set_mode(WIFI_MODE_STA);
}

esp_err_t wifi_mgr_set_sta_creds(const char *ssid, const char *pass)
{
    wifi_config_t sta_cfg = {0};
    strlcpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, pass, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    sta_cfg.sta.pmf_cfg.capable = true;
    sta_cfg.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
}

esp_err_t wifi_mgr_connect(void)
{
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "esp_wifi_connect: %s", esp_err_to_name(err));
    }
    return err;
}

bool wifi_mgr_is_connected(void)
{
    return s_connected;
}

bool wifi_mgr_get_sta_ip(char *buf, size_t size)
{
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta == NULL) {
        return false;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(sta, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    snprintf(buf, size, IPSTR, IP2STR(&info.ip));
    return true;
}
