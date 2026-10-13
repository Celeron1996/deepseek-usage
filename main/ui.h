#pragma once

#include <stdbool.h>

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
typedef enum {
    UI_GIF_NORMAL = 0, /* idle animation */
    UI_GIF_CHARGE,     /* balance increased */
    UI_GIF_EXPEND,     /* balance decreased */
} ui_gif_t;

void ui_show_balance(void);
void ui_balance_set_amount(const char *text);
void ui_balance_set_wifi(bool connected); /* small icon: green wifi / gray wifi + red slash */
void ui_balance_set_gif(ui_gif_t gif);
/* Register a callback invoked from the LVGL task once a special scene
 * (charge/expend) finished its loop count. Use it to trigger a re-check. */
void ui_set_special_gif_done_cb(void (*cb)(void));
void ui_balance_set_api(const char *text);
void ui_balance_set_url(const char *text);

#ifdef __cplusplus
}
#endif
