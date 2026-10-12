#include "webserver.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "config_store.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static const char *TAG = "webserver";

static QueueHandle_t s_evt_q;
static httpd_handle_t s_server;

/* ------------------------------ helpers ------------------------------ */

static void html_escape(const char *src, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (const char *p = src; *p && j + 7 < dst_size; p++) {
        switch (*p) {
        case '&':  j += snprintf(dst + j, dst_size - j, "&amp;");  break;
        case '<':  j += snprintf(dst + j, dst_size - j, "&lt;");   break;
        case '>':  j += snprintf(dst + j, dst_size - j, "&gt;");   break;
        case '"':  j += snprintf(dst + j, dst_size - j, "&quot;"); break;
        case '\'': j += snprintf(dst + j, dst_size - j, "&#39;");  break;
        default:   dst[j++] = *p; break;
        }
    }
    dst[j] = '\0';
}

static void url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '+') {
            *w++ = ' ';
        } else if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = { r[1], r[2], '\0' };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static bool form_get_field(const char *body, const char *name, char *out, size_t out_size)
{
    size_t nlen = strlen(name);
    const char *p = body;

    while (p && *p) {
        const char *amp = strchr(p, '&');
        size_t plen = amp ? (size_t)(amp - p) : strlen(p);
        const char *eq = memchr(p, '=', plen);
        if (eq && (size_t)(eq - p) == nlen && strncmp(p, name, nlen) == 0) {
            size_t vlen = plen - (size_t)(eq + 1 - p);
            if (vlen >= out_size) {
                vlen = out_size - 1;
            }
            memcpy(out, eq + 1, vlen);
            out[vlen] = '\0';
            url_decode(out);
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

static void trim_inplace(char *s)
{
    char *start = s;
    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

/* ------------------------------ pages ------------------------------ */

static const char PAGE_HEAD[] =
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>DeepSeek Balance Setup</title><style>"
    "body{font-family:-apple-system,'Segoe UI',Roboto,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px;}"
    "h2{margin:0 0 4px;font-size:20px;color:#38bdf8;}"
    ".sub{font-size:13px;color:#64748b;margin-bottom:16px;}"
    "label{display:block;margin:14px 0 6px;font-size:14px;color:#94a3b8;}"
    "input{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #334155;"
    "background:#0f172a;color:#e2e8f0;font-size:16px;}"
    "button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:8px;background:#38bdf8;"
    "color:#0b1220;font-size:16px;font-weight:600;}"
    "a{color:#38bdf8;}"
    ".err{color:#f87171;font-size:15px;line-height:1.6;}"
    "</style></head><body><div style=\"max-width:420px;margin:0 auto\">";

static esp_err_t send_page(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    static char page[3072];
    char ssid[64] = {0};
    char ssid_html[128] = {0};

    config_store_get_wifi(ssid, sizeof(ssid), NULL, 0); /* ssid may not exist */
    html_escape(ssid, ssid_html, sizeof(ssid_html));

    snprintf(page, sizeof(page),
             "%s<h2>DeepSeek Balance</h2>"
             "<div class=\"sub\">The device will connect to Wi-Fi and show your balance</div>"
             "<form method=\"POST\" action=\"/save\">"
             "<label>Wi-Fi name (SSID)</label>"
             "<input name=\"ssid\" maxlength=\"32\" required value=\"%s\">"
             "<label>Wi-Fi password</label>"
             "<input name=\"pass\" type=\"password\" maxlength=\"64\" placeholder=\"leave empty if open network\">"
             "<label>DeepSeek API key</label>"
             "<input name=\"key\" maxlength=\"128\" required placeholder=\"sk-...\">"
             "<button type=\"submit\">Save and Connect</button></form>"
             "<div class=\"sub\" style=\"margin-top:16px\">The setup URL is shown at the bottom of the "
             "device screen. Hold the device button for 5 s to clear the configuration and restart.</div>"
             "</div></body></html>",
             PAGE_HEAD, ssid_html);
    return send_page(req, page);
}

static esp_err_t send_result(httpd_req_t *req, const char *title, const char *body)
{
    static char page[2048];
    snprintf(page, sizeof(page),
             "%s<h2>%s</h2><div class=\"sub\" style=\"font-size:15px;line-height:1.6\">%s</div>"
             "<div style=\"margin-top:16px\"><a href=\"/\">Back to setup page</a></div>"
             "</div></body></html>",
             PAGE_HEAD, title, body);
    return send_page(req, page);
}

static void post_event(app_event_t type, const char *msg)
{
    app_msg_t m = { .type = type };
    if (msg) {
        strlcpy(m.msg, msg, sizeof(m.msg));
    }
    xQueueSend(s_evt_q, &m, 0);
}

static esp_err_t save_post_handler(httpd_req_t *req)
{
    char body[512];
    int total = req->content_len;
    if (total > (int)sizeof(body) - 1) {
        total = sizeof(body) - 1;
    }
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, body + received, total - received);
        if (r <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv failed");
            return ESP_FAIL;
        }
        received += r;
    }
    body[received] = '\0';

    char ssid[64] = {0};
    char pass[96] = {0};
    char key[160] = {0};
    form_get_field(body, "ssid", ssid, sizeof(ssid));
    form_get_field(body, "pass", pass, sizeof(pass));
    form_get_field(body, "key", key, sizeof(key));

    char err[128];
    if (!config_validate_creds(ssid, pass, err, sizeof(err))) {
        ESP_LOGW(TAG, "rejected credentials: %s", err);
        post_event(APP_EVT_WIFI_CREDS_INVALID, err);
        return send_result(req, "Configuration rejected", err);
    }

    trim_inplace(key);
    if (strlen(key) < 8) {
        snprintf(err, sizeof(err), "API key required (at least 8 characters)");
        ESP_LOGW(TAG, "rejected api key");
        post_event(APP_EVT_WIFI_CREDS_INVALID, err);
        return send_result(req, "Configuration rejected", err);
    }
    if (strpbrk(key, " \t\r\n") != NULL) {
        snprintf(err, sizeof(err), "API key must not contain spaces");
        ESP_LOGW(TAG, "rejected api key (whitespace)");
        post_event(APP_EVT_WIFI_CREDS_INVALID, err);
        return send_result(req, "Configuration rejected", err);
    }

    config_store_set_wifi(ssid, pass);
    config_store_set_api_key(key);
    ESP_LOGI(TAG, "new configuration stored (ssid=%s)", ssid);

    post_event(APP_EVT_WIFI_CREDS_SAVED, NULL);
    return send_result(req, "Configuration saved",
                       "The device is connecting to Wi-Fi, watch the progress on its screen. "
                       "The balance refreshes automatically once connected.");
}

/* Any unknown path (captive portal probes, favicon, ...) redirects to the
 * configuration page, which triggers the "sign in to network" popup on
 * Android/iOS. */
static esp_err_t captive_redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t webserver_start(QueueHandle_t event_queue)
{
    s_evt_q = event_queue;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 4;
    cfg.lru_purge_enable = true;

    ESP_ERROR_CHECK(httpd_start(&s_server, &cfg));

    const httpd_uri_t uri_root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_get_handler,
        .user_ctx = NULL,
    };
    const httpd_uri_t uri_save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_post_handler,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_root));
    ESP_ERROR_CHECK(httpd_register_uri_handler(s_server, &uri_save));
    ESP_ERROR_CHECK(httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, captive_redirect_handler));

    ESP_LOGI(TAG, "web server started");
    return ESP_OK;
}

/* --------------------------- captive portal DNS --------------------------- */

static volatile bool s_dns_run;
static int s_dns_sock = -1;
static TaskHandle_t s_dns_task;
static uint32_t s_dns_ip;

static void dns_task(void *arg)
{
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(53),
    };

    s_dns_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_dns_sock < 0) {
        ESP_LOGE(TAG, "dns socket failed: %d", errno);
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (bind(s_dns_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "dns bind failed: %d", errno);
        close(s_dns_sock);
        s_dns_sock = -1;
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 0, .tv_usec = 500 * 1000 };
    setsockopt(s_dns_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "captive portal dns started");

    uint8_t req[512];
    uint8_t resp[640];

    while (s_dns_run) {
        struct sockaddr_storage src;
        socklen_t slen = sizeof(src);
        int len = recvfrom(s_dns_sock, req, sizeof(req), 0, (struct sockaddr *)&src, &slen);
        if (len < 12) {
            continue;
        }

        /* parse the question section: skip labels, then qtype/qclass */
        int p = 12;
        while (p < len && req[p] != 0) {
            if ((req[p] & 0xC0) != 0) { /* compression not expected in a question */
                p = len;
                break;
            }
            p += req[p] + 1;
        }
        if (p + 5 > len) {
            continue;
        }
        uint16_t qtype = (uint16_t)((req[p + 1] << 8) | req[p + 2]);
        int question_end = p + 5;

        uint8_t answer[16] = {
            0xC0, 0x0C,             /* name pointer to the question */
            0x00, 0x01,             /* type A */
            0x00, 0x01,             /* class IN */
            0x00, 0x00, 0x00, 0x3C, /* ttl 60s */
            0x00, 0x04,             /* rdlength */
            0, 0, 0, 0,             /* ip placeholder */
        };
        int answer_len = (qtype == 1) ? (int)sizeof(answer) : 0;
        if (question_end + answer_len > (int)sizeof(resp)) {
            continue; /* cannot happen for valid queries, defensive */
        }

        memcpy(resp, req, question_end);
        resp[2] = 0x81; /* QR=1, RD=1 */
        resp[3] = 0x80; /* RA=1, RCODE=0 */
        resp[6] = 0; resp[7] = 0;
        resp[8] = 0; resp[9] = 0;
        resp[10] = 0; resp[11] = 0;

        if (answer_len > 0) {
            resp[7] = 1;
            memcpy(answer + 12, &s_dns_ip, 4);
            memcpy(resp + question_end, answer, answer_len);
        }

        sendto(s_dns_sock, resp, question_end + answer_len, 0, (struct sockaddr *)&src, slen);
    }

    close(s_dns_sock);
    s_dns_sock = -1;
    s_dns_task = NULL;
    ESP_LOGI(TAG, "captive portal dns stopped");
    vTaskDelete(NULL);
}

esp_err_t captive_portal_start(const char *ap_ip)
{
    if (s_dns_task != NULL) {
        return ESP_OK;
    }
    s_dns_ip = inet_addr(ap_ip);
    s_dns_run = true;
    xTaskCreate(dns_task, "captive_dns", 3072, NULL, 5, &s_dns_task);
    return ESP_OK;
}

void captive_portal_stop(void)
{
    if (s_dns_task == NULL) {
        return;
    }
    s_dns_run = false;
    while (s_dns_sock >= 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
