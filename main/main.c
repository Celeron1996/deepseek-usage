#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app.h"
#include "balance.h"
#include "button.h"
#include "config_store.h"
#include "display.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "ntp_time.h"
#include "ui.h"
#include "webserver.h"
#include "wifi_mgr.h"

static const char *TAG = "deepseek";

#define WIFI_CONNECT_ATTEMPTS       3
#define WIFI_CONNECT_TIMEOUT_MS     15000
#define WIFI_RECONNECT_INTERVAL_MS  5000
#define BALANCE_REFRESH_INTERVAL_MS 10000

typedef enum {
    ST_SETUP,     /* screen 1: SoftAP + web provisioning */
    ST_CONNECTING,/* screen 2: connecting with retries */
    ST_BALANCE,   /* screen 3: balance + clock */
} app_state_t;

typedef enum {
    CONNECT_OK,
    CONNECT_FAIL,
    CONNECT_INVALID, /* credentials rejected by the web form */
} connect_rc_t;

static QueueHandle_t s_q;
static char s_ap_ssid[33];
static const char *s_ap_url = "http://192.168.4.1";
static bool s_ap_running;
static bool s_first_setup; /* failure -> back to screen 1 instead of screen 3 */
static TaskHandle_t s_balance_task;
static TickType_t s_last_reconnect;

/* ----------------------------- screen helpers ----------------------------- */

static void on_wifi_up(void)
{
    ntp_time_start_once();

    char ip[20];
    if (wifi_mgr_get_sta_ip(ip, sizeof(ip))) {
        char url[48];
        snprintf(url, sizeof(url), "config: http://%s", ip);
        ui_balance_set_url(url);
    }
}

static void start_ap_mode(void)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "DeepSeek-%02X%02X", mac[4], mac[5]);

    ESP_ERROR_CHECK(wifi_mgr_start_ap(s_ap_ssid));
    ESP_ERROR_CHECK(captive_portal_start("192.168.4.1"));
    s_ap_running = true;

    ESP_LOGI(TAG, "setup mode: AP=%s url=%s", s_ap_ssid, s_ap_url);
}

/* ----------------------------- balance task ----------------------------- */

static long balance_to_cents(const char *text)
{
    double v = strtod(text, NULL);
    if (v < 0) {
        v = 0;
    }
    return (long)(v * 100.0 + 0.5);
}

static void balance_task(void *arg)
{
    char key[160];
    static long s_last_cents = -1;

    while (true) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BALANCE_REFRESH_INTERVAL_MS));

        if (!wifi_mgr_is_connected()) {
            ui_balance_set_api("Waiting for network...");
            continue;
        }

        /* TLS verifies certificate dates, wait for a sane clock first */
        if (!ntp_time_synced()) {
            ui_balance_set_api("Syncing time...");
            for (int i = 0; i < 15 && !ntp_time_synced() && wifi_mgr_is_connected(); i++) {
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }

        if (config_store_get_api_key(key, sizeof(key)) != ESP_OK || key[0] == '\0') {
            ui_balance_set_amount("--");
            ui_balance_set_api("No API key set");
            continue;
        }

        balance_info_t info;
        balance_result_t r = balance_fetch(key, &info);
        if (r == BALANCE_OK) {
            char text[40];
            snprintf(text, sizeof(text), "%s %s", info.currency, info.total);
            ui_balance_set_amount(text);

            /* pick the mascot animation from the balance change */
            long cents = balance_to_cents(info.total);
            if (s_last_cents < 0 || cents == s_last_cents) {
                ui_balance_set_gif(UI_GIF_NORMAL);
            } else if (cents > s_last_cents) {
                ui_balance_set_gif(UI_GIF_CHARGE);
                ui_balance_flash_gif(UI_GIF_CHARGE);
                ESP_LOGI(TAG, "balance up: charge scene");
            } else {
                ui_balance_set_gif(UI_GIF_EXPEND);
                ui_balance_flash_gif(UI_GIF_EXPEND);
                ESP_LOGI(TAG, "balance down: expend scene");
            }
            s_last_cents = cents;

            /* last refresh time (falls back before NTP is synced) */
            time_t now = time(NULL);
            struct tm tm_now;
            localtime_r(&now, &tm_now);
            if (tm_now.tm_year + 1900 >= 2024) {
                char upd[48];
                snprintf(upd, sizeof(upd), "Updated %02d:%02d:%02d",
                         tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
                ui_balance_set_api(upd);
            } else {
                ui_balance_set_api("Balance updated");
            }
            ESP_LOGI(TAG, "balance: %s %s", info.currency, info.total);
        } else {
            switch (r) {
            case BALANCE_ERR_API_KEY:
                ui_balance_set_amount("--");
                ui_balance_set_api("API key invalid or expired");
                break;
            case BALANCE_ERR_HTTP: {
                char buf[64];
                snprintf(buf, sizeof(buf), "HTTP error (%d)", info.http_status);
                ui_balance_set_api(buf);
                break;
            }
            case BALANCE_ERR_NETWORK:
                ui_balance_set_api("Network error, retrying...");
                break;
            default:
                ui_balance_set_api("Invalid response");
                break;
            }
            /* a special scene must not stay frozen on errors */
            ui_balance_set_gif(UI_GIF_NORMAL);
        }
    }
}

static void balance_recheck_cb(void)
{
    /* called from the LVGL task when a special scene finished its loops */
    if (s_balance_task != NULL) {
        xTaskNotifyGive(s_balance_task);
    }
}

static void enter_balance_screen(void)
{
    s_first_setup = false;

    ui_show_balance();
    if (wifi_mgr_is_connected()) {
        ui_balance_set_wifi(true);
        on_wifi_up();
    } else {
        ui_balance_set_wifi(false);
        ui_balance_set_url("config: waiting network");
    }
    ui_balance_set_amount("--");
    ui_balance_set_api("Fetching balance...");

    if (s_balance_task == NULL) {
        xTaskCreate(balance_task, "balance", 8192, NULL, 4, &s_balance_task);
        ui_set_special_gif_done_cb(balance_recheck_cb);
    }
    xTaskNotifyGive(s_balance_task);
}

/* --------------------------- connecting sequence --------------------------- */

static connect_rc_t do_connect_sequence(char *ssid, size_t ssid_size, char *pass, size_t pass_size)
{
    int attempt = 0;

    while (attempt < WIFI_CONNECT_ATTEMPTS) {
        attempt++;
        ui_connecting_status("Connecting to %s ...\n(attempt %d/%d)",
                             ssid, attempt, WIFI_CONNECT_ATTEMPTS);
        wifi_mgr_connect();

        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS);
        bool done = false, success = false, restart = false, invalid = false;

        while (!done) {
            TickType_t now = xTaskGetTickCount();
            if (now >= deadline) {
                break; /* timeout counts as a failed attempt */
            }

            app_msg_t ev;
            if (xQueueReceive(s_q, &ev, deadline - now) != pdTRUE) {
                break;
            }

            switch (ev.type) {
            case APP_EVT_STA_GOT_IP:
                success = true;
                done = true;
                break;
            case APP_EVT_STA_DISCONNECTED:
                done = true;
                break;
            case APP_EVT_WIFI_CREDS_SAVED:
                /* user changed the credentials while we were connecting */
                config_store_get_wifi(ssid, ssid_size, pass, pass_size);
                wifi_mgr_set_sta_creds(ssid, pass);
                attempt = 0;
                restart = true;
                done = true;
                break;
            case APP_EVT_WIFI_CREDS_INVALID:
                ui_connecting_status("%s", ev.msg);
                vTaskDelay(pdMS_TO_TICKS(3000));
                invalid = true;
                done = true;
                break;
            default:
                break;
            }
        }

        if (invalid) {
            return CONNECT_INVALID;
        }
        if (success) {
            return CONNECT_OK;
        }
        if (restart) {
            continue;
        }
        if (attempt < WIFI_CONNECT_ATTEMPTS) {
            ui_connecting_status("Connection failed, retrying (%d/%d)", attempt, WIFI_CONNECT_ATTEMPTS);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    return CONNECT_FAIL;
}

/* -------------------------------- app task -------------------------------- */

static void app_task(void *arg)
{
    char ssid[64] = {0};
    char pass[96] = {0};
    app_state_t state;

    s_first_setup = !config_store_has_wifi();

    if (s_first_setup) {
        start_ap_mode();
        ui_show_setup(s_ap_ssid, s_ap_url);
        state = ST_SETUP;
    } else {
        config_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
        ESP_LOGI(TAG, "stored wifi found: ssid=%s", ssid);
        wifi_mgr_set_sta_creds(ssid, pass);
        ui_show_connecting();
        ui_connecting_status("Connecting to %s ...", ssid);
        state = ST_CONNECTING;
    }

    while (true) {
        if (state == ST_SETUP) {
            app_msg_t ev;
            if (xQueueReceive(s_q, &ev, portMAX_DELAY) != pdTRUE) {
                continue;
            }
            if (ev.type == APP_EVT_WIFI_CREDS_SAVED) {
                config_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
                wifi_mgr_set_sta_creds(ssid, pass);
                ESP_LOGI(TAG, "provisioning: connecting to %s", ssid);
                ui_show_connecting();
                state = ST_CONNECTING;
            } else if (ev.type == APP_EVT_WIFI_CREDS_INVALID) {
                ui_show_connecting();
                ui_connecting_status("%s", ev.msg);
                vTaskDelay(pdMS_TO_TICKS(3000));
                ui_show_setup(s_ap_ssid, s_ap_url);
            }
            continue;
        }

        if (state == ST_CONNECTING) {
            connect_rc_t rc = do_connect_sequence(ssid, sizeof(ssid), pass, sizeof(pass));

            if (rc == CONNECT_OK) {
                if (s_ap_running) {
                    captive_portal_stop();
                    wifi_mgr_stop_ap();
                    s_ap_running = false;
                }
                ESP_LOGI(TAG, "wifi connected, entering balance screen");
                enter_balance_screen();
                state = ST_BALANCE;
            } else if (rc == CONNECT_INVALID) {
                ESP_LOGW(TAG, "credentials rejected");
                if (s_first_setup) {
                    ui_show_setup(s_ap_ssid, s_ap_url);
                    state = ST_SETUP;
                } else {
                    enter_balance_screen();
                    state = ST_BALANCE;
                }
            } else { /* CONNECT_FAIL */
                ESP_LOGW(TAG, "wifi connection failed");
                if (s_first_setup) {
                    ui_connecting_status("Unable to connect to Wi-Fi\nCheck the settings in the web page");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    ui_show_setup(s_ap_ssid, s_ap_url);
                    state = ST_SETUP;
                } else {
                    enter_balance_screen();
                    state = ST_BALANCE;
                }
            }
            continue;
        }

        /* ST_BALANCE: keep the web server reachable, reconnect forever */
        app_msg_t ev;
        if (xQueueReceive(s_q, &ev, pdMS_TO_TICKS(1000)) == pdTRUE) {
            switch (ev.type) {
            case APP_EVT_STA_GOT_IP:
                ESP_LOGI(TAG, "wifi reconnected");
                ui_balance_set_wifi(true);
                on_wifi_up();
                xTaskNotifyGive(s_balance_task);
                break;
            case APP_EVT_STA_DISCONNECTED:
                ui_balance_set_wifi(false);
                ui_balance_set_url("config: waiting network");
                s_last_reconnect = xTaskGetTickCount();
                break;
            case APP_EVT_BUTTON:
                xTaskNotifyGive(s_balance_task); /* manual refresh */
                break;
            case APP_EVT_WIFI_CREDS_SAVED:
                config_store_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
                wifi_mgr_set_sta_creds(ssid, pass);
                ESP_LOGI(TAG, "reconfiguring: connecting to %s", ssid);
                ui_show_connecting();
                state = ST_CONNECTING;
                break;
            case APP_EVT_WIFI_CREDS_INVALID:
                ui_show_connecting();
                ui_connecting_status("%s", ev.msg);
                vTaskDelay(pdMS_TO_TICKS(3000));
                enter_balance_screen();
                break;
            default:
                break;
            }
        }

        if (!wifi_mgr_is_connected() &&
            (xTaskGetTickCount() - s_last_reconnect) > pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS)) {
            s_last_reconnect = xTaskGetTickCount();
            wifi_mgr_connect();
        }
    }
}

/* ------------------------------- button task ------------------------------- */

static void button_task(void *arg)
{
    while (true) {
        int rc = button_wait(portMAX_DELAY);
        if (rc == BUTTON_SHORT_PRESS) {
            app_msg_t ev = { .type = APP_EVT_BUTTON };
            xQueueSend(s_q, &ev, 0);
        } else if (rc == BUTTON_LONG_PRESS) {
            ESP_LOGW(TAG, "factory reset: clearing wifi and api key");
            config_store_erase_all();
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        }
    }
}

/* --------------------------------- app_main -------------------------------- */

void app_main(void)
{
    ESP_ERROR_CHECK(config_store_init());

    ESP_ERROR_CHECK(display_init());
    ui_init();

    s_q = xQueueCreate(APP_QUEUE_LEN, sizeof(app_msg_t));
    ESP_ERROR_CHECK(s_q != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(wifi_mgr_init(s_q));
    ESP_ERROR_CHECK(webserver_start(s_q));
    ESP_ERROR_CHECK(button_init());

    xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);
    xTaskCreate(app_task, "app", 6144, NULL, 6, NULL);
}
