#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BALANCE_OK = 0,      /* balance parsed successfully */
    BALANCE_ERR_API_KEY, /* 401/403: key invalid or expired */
    BALANCE_ERR_NETWORK, /* connection/TLS failed (no internet?) */
    BALANCE_ERR_HTTP,    /* other HTTP status code */
    BALANCE_ERR_PARSE,   /* unexpected response body */
} balance_result_t;

typedef struct {
    balance_result_t result;
    int http_status;
    char currency[8];
    char total[32];
} balance_info_t;

/* Query https://api.deepseek.com/user/balance */
balance_result_t balance_fetch(const char *api_key, balance_info_t *info);

#ifdef __cplusplus
}
#endif
