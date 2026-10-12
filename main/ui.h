#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Create the LVGL clock timer (call once, after display_init). */
void ui_init(void);

/* Screen 1: provisioning help (SoftAP SSID and configuration URL). */
void ui_show_setup(const char *ap_ssid, const char *url);

/* Screen 2: connecting progress. */
void ui_show_connecting(void);
void ui_connecting_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Screen 3: DeepSeek balance. */
void ui_show_balance(void);
void ui_balance_set_amount(const char *text);
void ui_balance_set_wifi(const char *text);
void ui_balance_set_api(const char *text);
void ui_balance_set_url(const char *text);

#ifdef __cplusplus
}
#endif
