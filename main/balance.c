#include "balance.h"

#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "balance";

#define DEEPSEEK_BALANCE_URL "https://api.deepseek.com/user/balance"
#define DEEPSEEK_HOST        "api.deepseek.com"

/* Diagnostics: log the resolved address and try a raw TCP connect. */
static void network_probe(void)
{
    ESP_LOGW(TAG, "heap: free=%u largest_internal=%u",
             (unsigned)esp_get_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(DEEPSEEK_HOST, NULL, &hints, &res) != 0 || res == NULL) {
        ESP_LOGW(TAG, "dns %s failed", DEEPSEEK_HOST);
        return;
    }
    struct in_addr addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    ESP_LOGW(TAG, "dns %s -> %s", DEEPSEEK_HOST, inet_ntoa(addr));

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s >= 0) {
        struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        struct sockaddr_in dst = {
            .sin_family = AF_INET,
            .sin_addr = addr,
            .sin_port = htons(443),
        };
        int rc = connect(s, (struct sockaddr *)&dst, sizeof(dst));
        ESP_LOGW(TAG, "tcp probe %s:443 -> %s", inet_ntoa(addr), rc == 0 ? "OK" : "FAILED");
        close(s);
    }
    freeaddrinfo(res);
}

static balance_result_t parse_balance(const char *body, balance_info_t *info)
{
    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        ESP_LOGW(TAG, "json parse failed");
        return BALANCE_ERR_PARSE;
    }

    balance_result_t result = BALANCE_ERR_PARSE;
    const cJSON *infos = cJSON_GetObjectItem(root, "balance_infos");
    if (cJSON_IsArray(infos) && cJSON_GetArraySize(infos) > 0) {
        const cJSON *entry = cJSON_GetArrayItem(infos, 0);
        const cJSON *currency = cJSON_GetObjectItem(entry, "currency");
        const cJSON *total = cJSON_GetObjectItem(entry, "total_balance");
        if (cJSON_IsString(currency) && cJSON_IsString(total)) {
            strlcpy(info->currency, currency->valuestring, sizeof(info->currency));
            strlcpy(info->total, total->valuestring, sizeof(info->total));
            result = BALANCE_OK;
        }
    }

    cJSON_Delete(root);
    return result;
}

balance_result_t balance_fetch(const char *api_key, balance_info_t *info)
{
    memset(info, 0, sizeof(*info));

    char auth[192];
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);

    const esp_http_client_config_t cfg = {
        .url = DEEPSEEK_BALANCE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 1024,
        .buffer_size_tx = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        info->result = BALANCE_ERR_NETWORK;
        return info->result;
    }
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "User-Agent", "esp32s2-deepseek-balance/1.0");

    balance_result_t result;
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "https open failed: %s (errno=%d)", esp_err_to_name(err),
                 esp_http_client_get_errno(client));
        network_probe();
        result = BALANCE_ERR_NETWORK;
    } else {
        int64_t content_len = esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        info->http_status = status;

        if (content_len < 0) {
            ESP_LOGW(TAG, "no response headers");
            result = BALANCE_ERR_NETWORK;
        } else if (status == 401 || status == 403) {
            ESP_LOGW(TAG, "api key rejected (http %d)", status);
            result = BALANCE_ERR_API_KEY;
        } else if (status != 200) {
            ESP_LOGW(TAG, "unexpected http status %d", status);
            result = BALANCE_ERR_HTTP;
        } else {
            char body[768];
            int r = esp_http_client_read_response(client, body, sizeof(body) - 1);
            if (r <= 0) {
                result = BALANCE_ERR_NETWORK;
            } else {
                body[r] = '\0';
                ESP_LOGD(TAG, "response: %s", body);
                result = parse_balance(body, info);
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    info->result = result;
    return result;
}
